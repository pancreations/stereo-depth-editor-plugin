#include "s3d/pipeline.hpp"
#include <iostream>

int main(int argc,char** argv) {
  try {
    if(argc<5) {
      std::cerr<<"Usage: s3d_render color.pfm depth.pfm|frame.s3d output.pfm half|full [strength] [convergence] [normalize_low normalize_high]\n";return 2;
    }
    s3d::Settings settings;
    std::string mode=argv[4];
    if(mode!="full" && mode!="half") throw std::invalid_argument("Output must be half or full");
    settings.output=mode=="full"?s3d::OutputMode::FullSBS:s3d::OutputMode::HalfSBS;
    if(argc>5) settings.stereo.strength=std::stof(argv[5]);
    if(argc>6) settings.stereo.convergence=std::stof(argv[6]);
    std::filesystem::path depth_path=argv[2];
    auto depth=depth_path.extension()==".s3d"?s3d::read_depth_cache(depth_path):s3d::read_pfm_depth(depth_path);
    if(argc>8) {
      settings.depth.normalize=true;settings.depth.normalization_low=std::stof(argv[7]);
      settings.depth.normalization_high=std::stof(argv[8]);
    }
    auto result=s3d::render(s3d::read_pfm_image(argv[1]),depth,settings);
    s3d::write_pfm(result.output,argv[3]);
    std::cout<<result.output.width<<"x"<<result.output.height<<" written\n";return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
