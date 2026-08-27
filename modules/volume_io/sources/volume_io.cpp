// ============================================================================
// volume_io.cpp — DICOM series loader (GDCM implementation).
// ============================================================================
//
// Reads a directory of single-frame DICOM slices, sorts them into volume order,
// stacks them into one buffer, and reports the metadata the renderer needs. The
// renderer never sees GDCM — only the flat VolumeData struct (see volume_io.h).
//
// Output contract (uniform regardless of the source's bit depth / signedness):
//   - voxels are stored as uint16 = round(HU) + OFFSET, where HU is the
//     Hounsfield value (raw * RescaleSlope + RescaleIntercept). OFFSET shifts
//     air (-1024 HU) to 0 so negative HU survive an unsigned buffer.
//   - rescale_slope = 1, rescale_intercept = -OFFSET, so any consumer recovers
//     HU = stored * slope + intercept. window_center/width stay in HU.
// This keeps the engine simple: it always sees "stored = HU + 1024, slope 1".
//
// Limitations (fine for our synthetic series; revisit for real-world data):
//   - one frame per file (classic CT/MR series), not multi-frame enhanced DICOM;
//   - scalar (MONOCHROME) pixels only — RGB/palette are zeroed;
//   - 8/16-bit integer pixels (the CT/MR norm); other formats are zeroed.

#include "volume_io.h"
#include <string>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef HAVE_GDCM

#  include <gdcmByteValue.h>
#  include <gdcmDataElement.h>
#  include <gdcmDataSet.h>
#  include <gdcmDirectory.h>
#  include <gdcmFile.h>
#  include <gdcmIPPSorter.h>
#  include <gdcmImage.h>
#  include <gdcmImageReader.h>
#  include <gdcmPixelFormat.h>
#  include <gdcmTag.h>

#  include <algorithm>
#  include <cmath>
#  include <string>
#  include <vector>

namespace {

  // Air sits at -1024 HU; shifting by +1024 keeps the smallest CT values >= 0.
  constexpr int kHuOffset = 1024;

  // Convert a typed source slice to "HU + offset" uint16, tracking the min/max.
  template <class T> void convert_typed(const char* raw, double slope, double intercept, std::size_t n, uint16_t* dst, int& vmin, int& vmax) {
    const T* src = reinterpret_cast<const T*>(raw);
    for (std::size_t i = 0; i < n; ++i) {
      const double hu = static_cast<double>(src[i]) * slope + intercept;
      int val = static_cast<int>(std::lround(hu)) + kHuOffset;
      if (val < 0)
        val = 0;
      else if (val > 65535)
        val = 65535;
      dst[i] = static_cast<uint16_t>(val);
      if (val < vmin) vmin = val;
      if (val > vmax) vmax = val;
    }
  }

  // Dispatch on the pixel format. Unsupported formats leave a zeroed slice.
  void convert_slice(const char* raw, const gdcm::PixelFormat& pf, double slope, double intercept, std::size_t n, uint16_t* dst, int& vmin,
                     int& vmax) {
    switch (pf.GetScalarType()) {
      case gdcm::PixelFormat::UINT8:
        convert_typed<uint8_t>(raw, slope, intercept, n, dst, vmin, vmax);
        break;
      case gdcm::PixelFormat::INT8:
        convert_typed<int8_t>(raw, slope, intercept, n, dst, vmin, vmax);
        break;
      case gdcm::PixelFormat::UINT16:
        convert_typed<uint16_t>(raw, slope, intercept, n, dst, vmin, vmax);
        break;
      case gdcm::PixelFormat::INT16:
        convert_typed<int16_t>(raw, slope, intercept, n, dst, vmin, vmax);
        break;
      default:
        std::memset(dst, 0, n * sizeof(uint16_t));
        break;
    }
  }

  // Read a decimal-string (DS) tag's first value. Robust to multi-valued DS
  // ("40\400") by letting atof stop at the backslash. Returns false if absent.
  bool read_ds(const gdcm::DataSet& ds, uint16_t group, uint16_t element, double& out) {
    const gdcm::Tag tag(group, element);
    if (!ds.FindDataElement(tag)) return false;
    const gdcm::ByteValue* bv = ds.GetDataElement(tag).GetByteValue();
    if (!bv || bv->GetLength() == 0) return false;
    std::string s(bv->GetPointer(), bv->GetLength());
    out = std::atof(s.c_str());
    return true;
  }

}  // namespace

extern "C" VolumeData volume_io_load_series(const char* dir) {
  VolumeData v;
  std::memset(&v, 0, sizeof(v));
  if (!dir) return v;

  // 1. Enumerate files in the directory (non-recursive).
  gdcm::Directory listing;
  if (listing.Load(dir, /*recursive=*/false) == 0) return v;
  const gdcm::Directory::FilenamesType& files = listing.GetFilenames();
  if (files.empty()) return v;

  // 2. Sort by ImagePositionPatient and compute the true inter-slice spacing.
  //    If sorting fails (e.g. missing positions), fall back to filename order.
  gdcm::IPPSorter sorter;
  sorter.SetComputeZSpacing(true);
  sorter.SetZSpacingTolerance(0.01);
  std::vector<std::string> sorted;
  double zspacing = 0.0;
  if (sorter.Sort(files)) {
    sorted = sorter.GetFilenames();
    zspacing = sorter.GetZSpacing();
  } else {
    sorted.assign(files.begin(), files.end());
    std::sort(sorted.begin(), sorted.end());
  }
  if (sorted.empty()) return v;

  // 3. Read the first slice for geometry + window metadata.
  gdcm::ImageReader r0;
  r0.SetFileName(sorted[0].c_str());
  if (!r0.Read()) return v;
  const gdcm::Image& img0 = r0.GetImage();
  const unsigned int w = img0.GetDimension(0);
  const unsigned int h = img0.GetDimension(1);
  const unsigned int depth = static_cast<unsigned int>(sorted.size());
  if (w == 0 || h == 0) return v;

  const double* sp = img0.GetSpacing();
  const double sx = sp[0];
  const double sy = sp[1];
  double sz = zspacing > 0.0 ? zspacing : img0.GetSpacing(2);
  if (sz <= 0.0) sz = 1.0;

  double wc = 0.0, ww = 0.0;
  const gdcm::DataSet& ds0 = r0.GetFile().GetDataSet();
  const bool have_window = read_ds(ds0, 0x0028, 0x1050, wc) && read_ds(ds0, 0x0028, 0x1051, ww);

  // 4. Allocate the stacked volume and fill it slice by slice.
  const std::size_t slice = static_cast<std::size_t>(w) * h;
  const std::size_t total = slice * depth;
  uint16_t* vox = static_cast<uint16_t*>(std::malloc(total * sizeof(uint16_t)));
  if (!vox) return v;

  int vmin = 65535, vmax = 0;
  std::vector<char> buf;
  for (unsigned int z = 0; z < depth; ++z) {
    gdcm::ImageReader r;
    r.SetFileName(sorted[z].c_str());
    if (!r.Read()) {
      std::free(vox);
      return v;
    }
    const gdcm::Image& im = r.GetImage();
    buf.resize(im.GetBufferLength());
    if (!im.GetBuffer(buf.data())) {
      std::free(vox);
      return v;
    }
    convert_slice(buf.data(), im.GetPixelFormat(), im.GetSlope(), im.GetIntercept(), slice, vox + static_cast<std::size_t>(z) * slice, vmin, vmax);
  }

  // 5. Fill the contract. HU is recovered as stored * 1 - kHuOffset.
  v.voxels = vox;
  v.width = static_cast<int>(w);
  v.height = static_cast<int>(h);
  v.depth = static_cast<int>(depth);
  v.spacing_x = static_cast<float>(sx);
  v.spacing_y = static_cast<float>(sy);
  v.spacing_z = static_cast<float>(sz);
  v.rescale_slope = 1.0f;
  v.rescale_intercept = static_cast<float>(-kHuOffset);
  if (have_window) {
    v.window_center = static_cast<float>(wc);
    v.window_width = static_cast<float>(ww);
  } else {
    // Sensible default: center the observed range (in HU), full width.
    v.window_center = static_cast<float>((vmin + vmax) / 2 - kHuOffset);
    v.window_width = static_cast<float>(std::max(1, vmax - vmin));
  }
  v.value_min = static_cast<float>(vmin);
  v.value_max = static_cast<float>(vmax);
  return v;
}

extern "C" void volume_io_free(VolumeData* v) {
  if (v && v->voxels) std::free(v->voxels);
  if (v) std::memset(v, 0, sizeof(*v));
}

#else  // !HAVE_GDCM — built without the DICOM dependency.

extern "C" VolumeData volume_io_load_series(const char* /*dir*/) {
  VolumeData v;
  std::memset(&v, 0, sizeof(v));
  return v;  // voxels == NULL signals "no loader available".
}

extern "C" void volume_io_free(VolumeData* v) {
  if (v && v->voxels) std::free(v->voxels);
  if (v) std::memset(v, 0, sizeof(*v));
}

#endif  // HAVE_GDCM
