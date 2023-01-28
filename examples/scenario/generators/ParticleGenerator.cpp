#include "ParticleGenerator.h"
#include "../FastNoiseLite.h"
#include "Random.h"
#include <iostream>
#include <iomanip>
std::vector<Color32> ParticleGenerator::Generate(int sideSize, float time) {
  // bootstrap condition
  if(sideSize!=sideSizeCached){
    sideSizeCached = sideSize;
    generateRandomHeights();
    lastUpdate=time;
  }
  else {
    float const dt = time - lastUpdate;
    lastUpdate = time;
    Erode(dt);
  }

  return heightsToColor();
}
std::string ParticleGenerator::GetName() { return "Particle-Based Hydraulic "
                                                  "Erosion"; }
std::vector<Color32> ParticleGenerator::heightsToColor() {
  std::vector<Color32> colors;
  for(int y=0; y<sideSizeCached; y++)
    for(int x=0; x<sideSizeCached; x++) {
      auto h = heights[y][x];
      if(h<0.3)
        colors.push_back(Color32::LerpColor(Color::DarkBlue, Color::Blue, h/0.3f));
      else if(h < 0.4)
        colors.push_back(Color32::LerpColor(Color::Blue, Color::Yellow, (h-0.3f)/0.1f));
      else if(h < 0.5)
        colors.push_back(Color32::LerpColor(Color::Yellow, Color::Green,
                                            (h-0.4f)/0.1f));
      else if(h < 0.7)
        colors.push_back(Color32::LerpColor(Color::Green, Color::SandyBrown,
                                            (h-0.5f)/0.2f));
      else
        colors.push_back(Color32::LerpColor(Color::SandyBrown, Color::White,
                                            (h-0.7f)/0.3f));
    }
  return colors;
}
void ParticleGenerator::generateRandomHeights() {

  float displacement = Random::Range(0,sideSizeCached);

  // noise
  FastNoiseLite noise;
  noise.SetFractalOctaves(4);
  noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2S);
  noise.SetFractalType(FastNoiseLite::FractalType_FBm);
  auto sqrt2 = sqrtf(2);
  for(int y=0; y<sideSizeCached;y++) {
    for (int x = 0; x < sideSizeCached; x++) {
      // island generation
      float const posY = (float)((y-(float)sideSizeCached/2))/((float)sideSizeCached/2);
      float const posX = (float)((x-(float)sideSizeCached/2))/((float)sideSizeCached/2);
      float const islandInfluence = (1-posX*posX)*(1-posY*posY);

      // noise
      float const noiseInfluence = (1 + noise.GetNoise((float)y, (float)x, displacement*50))/2;

      heights[y][x] = islandInfluence*.3f + noiseInfluence*.7f;
//      heights[y][x] = islandInfluence*.3f + noiseInfluence*.7f;
    }
  }
}
glm::vec3 ParticleGenerator::surfaceNormal(int i, int j) {
  float scale = 0.1;

  /*
    Note: Surface normal is computed in this way, because the square-grid surface is meshed using triangles.
    To avoid spatial artifacts, you need to weight properly with all neighbors.
  */
  static const float sqrt2 = sqrtf(2);

  glm::vec3 n = glm::vec3(0.15) * glm::normalize(glm::vec3(scale*(heights[i][j]- heights[i+1][j]), 1.0, 0.0));  //Positive X
  n += glm::vec3(0.15) * glm::normalize(glm::vec3(scale*(heights[i-1][j]- heights[i][j]), 1.0, 0.0));  //Negative X
  n += glm::vec3(0.15) * glm::normalize(glm::vec3(0.0, 1.0, scale*(heights[i][j]- heights[i][j+1])));    //Positive Y
  n += glm::vec3(0.15) * glm::normalize(glm::vec3(0.0, 1.0, scale*(heights[i][j-1]- heights[i][j])));  //Negative Y

  //Diagonals! (This removes the last spatial artifacts)
  n += glm::vec3(0.1) * glm::normalize(glm::vec3(scale*(heights[i][j]- heights[i+1][j+1])/sqrt2, sqrt2, scale*(heights[i][j]- heights[i+1][j+1])/sqrt2));    //Positive Y
  n += glm::vec3(0.1) * glm::normalize(glm::vec3(scale*(heights[i][j]- heights[i+1][j-1])/sqrt2, sqrt2, scale*(heights[i][j]- heights[i+1][j-1])/sqrt2));    //Positive Y
  n += glm::vec3(0.1) * glm::normalize(glm::vec3(scale*(heights[i][j]- heights[i-1][j+1])/sqrt2, sqrt2, scale*(heights[i][j]- heights[i-1][j+1])/sqrt2));    //Positive Y
  n += glm::vec3(0.1) * glm::normalize(glm::vec3(scale*(heights[i][j]- heights[i-1][j-1])/sqrt2, sqrt2, scale*(heights[i][j]- heights[i-1][j-1])/sqrt2));    //Positive Y

  // we can still see clearly as asterisk pattern. needs improvement
  return n;
}
void ParticleGenerator::Erode(float dt) {
  // the quality of this dt is awful pf...
  static int cycles = 1;
  if(dt>0.500) {
    if (cycles > 1) cycles--;
  }
  else
    cycles++;

  //Do a series of iterations! (5 Particles)
  for(int i = 0; i < cycles; i++){
    //Spawn New Particle
    glm::vec2 startPos = glm::vec2(Random::Range(1,(int)sideSizeCached-2),Random::Range(1,(int)sideSizeCached-2));
    Particle drop(startPos);

    glm::vec2 dim = glm::vec2(sideSizeCached,sideSizeCached);
    //As long as the droplet exists...
    while(drop.volume > minVol){
      //Check if Particle is still in-bounds
//      if(!glm::all(glm::greaterThanEqual(drop.pos, glm::vec2(0))) ||
//          !glm::all(glm::lessThan(drop.pos, dim)))
//        break;

      glm::ivec2 ipos = drop.pos;                   //Floored Droplet Initial Position

      if(ipos.x<=0||ipos.y<=0||ipos.x>=sideSizeCached-1||ipos.y>=sideSizeCached-1)
        break;

      glm::vec3 n = surfaceNormal(ipos.x, ipos.y);  //Surface Normal at Position

      //Accelerate particle using newtonian mechanics using the surface normal.
      drop.speed += dt*glm::vec2(n.x, n.z)/(drop.volume*density);//F = ma, so a = F/m
      drop.pos   += dt*drop.speed;
      drop.speed *= (1.0-dt*friction);       //Friction Factor

      /*
        Note: For multiplied factors (e.g. friction, evaporation)
        time-scaling is correctly implemented like above.
      */

      if((int)drop.pos.x<0||(int)drop.pos.y<0||(int)drop.pos.x>=sideSizeCached||(int)drop.pos.y>=sideSizeCached)
        break;

      //Compute sediment capacity difference
      float maxsediment = drop.volume*glm::length(drop.speed)*(heights[ipos.x][ipos.y]-heights[(int)drop.pos.x][(int)drop.pos.y]);
      if(maxsediment < 0.0) maxsediment = 0.0;
      float sdiff = maxsediment - drop.sediment;

      //Act on the heights and Droplet!
      drop.sediment += dt*depositionRate*sdiff;
      heights[ipos.x][ipos.y] -= dt*drop.volume*depositionRate*sdiff;

      //Evaporate the Droplet (Note: Proportional to Volume! Better: Use shape factor to make proportional to the area instead.)
      drop.volume *= (1.0f-dt*evapRate);
    }
    auto delta = drop.pos - startPos;
    std::cout << "[" << startPos.x << "," <<  startPos.y << "]" << "->[" << (int)drop.pos.x << "," <<  (int)drop.pos.y << "] angle: " << std::fixed << std::setprecision(2) << atan2(delta.y, delta.x)*(180 / 3.1415) << " dist: " << glm::length(delta) << std::endl;
  }
  std::cout << "Cycles: " << cycles << "dt: " << (int)(dt*1000)<<"ms" << std::endl;
}
