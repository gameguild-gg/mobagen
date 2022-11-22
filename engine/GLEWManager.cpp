//#include "GLEWManager.h"
//
//#ifndef ANDROID
//  #include <GL/glew.h>
//#endif
//
//#include "GLEWManager.h"
//
//#include "Logger.h"
//
//GLEWManager::GLEWManager(void)
//{
//#ifndef ANDROID
//  glewExperimental = GL_TRUE;
//  GLenum err = glewInit();
//
//  log_info("Status: Using GLEW %s", glewGetString(GLEW_VERSION));
//
//  // glew only exists after window initialization
//  if (GLEW_OK != err)
//  {
//    log_err("GLEW failed to initalize: %s", glewGetErrorString(err));
//  }
//#endif
//}
//
//GLEWManager::~GLEWManager(void)
//{
//}