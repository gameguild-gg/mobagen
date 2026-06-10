# Assets

Binary volume data is not stored in Git. This directory stores only the asset
manifest and notes.

Run:

```bash
make assets
```

That command downloads/verifies the public head volume, extracts the raw file,
generates the synthetic WebGL phantom, and creates the `.mvol` file used by the
WebGPU path.

Generated files are intentionally ignored:

- `apps/dicom_viewer/assets/cache/head256x256x109.zip`
- `apps/dicom_viewer/assets/head256x256x109.raw`
- `apps/dicom_viewer/assets/volume.raw`
- `apps/dicom_viewer/assets/volume.mvol`

The asset manifest records source URLs, hashes, dimensions, and generation
parameters. Treat this as the first version of the project asset manager: code
and recipes are versioned; bulky binary payloads are reproducible local state.
