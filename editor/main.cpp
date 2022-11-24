#define SDL_MAIN_HANDLED true
#include <Engine.h>
#include <vector>
//#include <gdcmReader.h>
//#include <gdcmDirectory.h>
//#include <map>

//void readDicom(std::vector<std::string> filenames) {
//  bool verbose = true;
//  bool recursive = true;
//  std::vector<std::string> names;
//  gdcm::Reader testreader;
//  // Is it a single directory ? If so loop over all files contained in it:
//  // const char *filename = argv[1];
//  if (filenames.size() == 1 &&
//      gdcm::System::FileIsDirectory(filenames[0].c_str())) {
//    if (verbose)
//      std::cout << "Loading directory: " << filenames[0] << std::endl;
//    gdcm::Directory d;
//    d.Load(filenames[0].c_str(), recursive);
//    gdcm::Directory::FilenamesType const& files = d.GetFilenames();
//    for (gdcm::Directory::FilenamesType::const_iterator it = files.begin();
//         it != files.end(); ++it) {
//      testreader.SetFileName(it->c_str());
//      if (testreader.CanRead()) {
//        names.push_back(it->c_str());
//      } else {
//        if (verbose)
//          std::cerr << "Discarding non DICOM file: " << it->c_str()
//                    << std::endl;
//      }
//    }
//  } else  // list of files passed directly on the cmd line:
//          // discard non-existing or directory
//  {
//    // for(int i=1; i < argc; ++i)
//    for (std::vector<std::string>::const_iterator it = filenames.begin();
//         it != filenames.end(); ++it) {
//      // filename = argv[i];
//      const std::string& filename = *it;
//      if (gdcm::System::FileExists(filename.c_str())) {
//        if (gdcm::System::FileIsDirectory(filename.c_str())) {
//          if (verbose)
//            std::cerr << "Discarding directory: " << filename << std::endl;
//        } else {
//          testreader.SetFileName(filename.c_str());
//          if (testreader.CanRead()) {
//            names.push_back(filename.c_str());
//          } else {
//            if (verbose)
//              std::cerr << "Discarding non DICOM file: " << filename.c_str()
//                        << std::endl;
//          }
//        }
//      } else {
//        if (verbose)
//          std::cerr << "Discarding non existing file: " << filename
//                    << std::endl;
//      }
//    }
//  }
//  // names->Print( std::cout );
//}


// Main code
int main(int, char**) {
    SDL_Log("Creating Engine");
    auto engine = new Engine();
    SDL_Log("Engine Created");

    SDL_Log("Starting Engine");
    if(engine->Start("Editor")) {
        SDL_Log("Engine Started");
        engine->Run();
    }

    SDL_Log("Exiting Engine");
    engine->Exit();
    SDL_Log("Engine Exited");
    return 0;
}