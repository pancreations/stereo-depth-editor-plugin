#include "s3d/pipeline.hpp"
#include <functional>
#include <iostream>
#include <string>

static int checks=0;
void check(bool ok,const char* name) {++checks;if(!ok) throw std::runtime_error(name);}
void near(float a,float b,const char* name) {check(std::abs(a-b)<1e-5f,name);}
void rejects(std::function<void()> f,const char* name) {try{f();}catch(const std::exception&){++checks;return;}throw std::runtime_error(name);}
int main() {
  using namespace s3d;
  try {
    Image image(100,7);Depth depth(100,7,0.5f);
    for(int y=0;y<7;++y) for(int x=0;x<100;++x) image.at(x,y)={float(x)/10,-0.3f,2.2f,1};
    Settings s;s.output=OutputMode::FullSBS;s.stereo.strength=0;
    auto r=render(image,depth,s);
    check(r.output.width==200 && r.output.height==7,"full dimensions");
    for(int y=0;y<7;++y) for(int x=0;x<200;++x) {
      near(r.output.at(x,y).r,image.at(x%100,y).r,"zero stereo identity");
      near(r.output.at(x,y).g,-0.3f,"negative color preserved");near(r.output.at(x,y).b,2.2f,"HDR preserved");
    }
    s.output=OutputMode::HalfSBS;r=render(image,depth,s);check(r.output.width==100,"half dimensions");
    Image odd(7,13,Pixel{2,3,4,1});Depth odd_depth(7,13,.5f);
    r=render(odd,odd_depth,s);check(r.output.width==7 && r.output.height==13,"arbitrary portrait dimensions");
    for(auto p:r.output.pixels) near(p.a,1,"odd packing no gaps");
    StereoSettings stereo;stereo.eye_separation_percent=20;stereo.max_parallax_percent=4;
    near(disparity(1,100,stereo),4,"parallax limited per pair");
    near(disparity(.5f,100,stereo),0,"convergence zero plane");
    stereo.reverse_depth=true;near(disparity(1,100,stereo),-4,"depth reverse");
    depth=Depth(100,7,1);stereo.reverse_depth=false;
    auto eye=reproject(image,depth,stereo,true);
    near(eye.image.at(10,2).r,image.at(8,2).r,"left eye direction");
    auto right=reproject(image,depth,stereo,false);
    near(right.image.at(10,2).r,image.at(12,2).r,"right eye direction");
    reconstruct(eye,image,{});for(auto p:eye.image.pixels) near(p.a,1,"holes repaired");
    check(eye.holes.at(0,0)==1,"original mask retained");
    RepairSettings repair;repair.mode=Reconstruction::AI;rejects([&]{reconstruct(eye,image,repair);},"missing AI must fail");
    auto high_eye=reproject(image,depth,stereo,true);
    repair.mode=Reconstruction::High;reconstruct(high_eye,image,repair);
    for(auto p:high_eye.image.pixels) near(p.a,1,"content fill preserves opaque coverage");
    struct FillProvider:InpaintingProvider {
      Image fill(const Image& warped,const Mask&,const Image&,const RepairSettings&) override {
        return Image(warped.width,warped.height,Pixel{8,7,6,1});
      }
    } provider;
    auto ai_eye=reproject(image,depth,stereo,true);repair.mode=Reconstruction::AI;
    reconstruct(ai_eye,image,repair,&provider);
    near(ai_eye.image.at(0,0).r,8,"provider fills holes");
    near(ai_eye.image.at(10,2).r,image.at(8,2).r,"provider cannot overwrite known pixels");
    Depth d(3,1);d.pixels={0,.5f,1};DepthSettings ds;
    auto remap=process_depth(d,ds);near(remap.at(1,0),.5f,"neutral contrast");
    ds.contrast=0;remap=process_depth(d,ds);for(float v:remap.pixels) near(v,.5f,"contrast flat");
    ds.contrast=1;ds.invert=true;remap=process_depth(d,ds);near(remap.at(0,0),1,"invert");
    Settings inverted;inverted.depth.invert=true;inverted.preview=Preview::DepthMap;
    Image guide(3,1,Pixel{2,3,4,1});
    auto preview=render(guide,d,inverted);
    near(preview.output.at(0,0).r,1,"depth preview shows inverted black as white");
    near(preview.output.at(2,0).r,0,"depth preview shows inverted white as black");
    near(preview.output.at(1,0).r,.5f,"invert preserves midpoint");
    near(d.at(0,0),0,"invert leaves raw cached depth unchanged");
    inverted.depth.white_near=false;preview=render(guide,d,inverted);
    near(preview.output.at(0,0).r,0,"black near interpretation and invert compose");
    ds.near_point=ds.far_point;rejects([&]{process_depth(d,ds);},"degenerate remap");
    rejects([]{Image bad(-1,4);},"invalid sizes");
    rejects([&]{pack_sbs(image,odd,OutputMode::HalfSBS);},"mismatched eyes");
    check(frame_at_time(1001,30000,30000,1001)==1,"NTSC rational time");
    check(frame_at_time(-1001,30000,30000,1001)==-1,"negative time");
    check(frame_at_time(254016000000LL,254016000000LL,24000,1001)==23,"Premiere ticks");
    TemporalSettings ts;ts.enabled=true;
    auto stable=stabilize(depth,Depth(100,7,0),image,image,ts,true);
    near(stable.at(1,1),1,"cut resets temporal");
    auto black=Image(100,7,Pixel{0,0,0,1});
    stable=stabilize(depth,Depth(100,7,0),image,black,ts);
    near(stable.at(1,1),1,"scene cut detection");
    Image l(4,2,Pixel{1,0,0,1}),rr(4,2,Pixel{0,1,0,1});
    auto packed=pack_sbs(l,rr,OutputMode::FullSBS,true);near(packed.at(0,0).g,1,"eye order");
    // Both modes: larger/smaller canvases, bars vs crop, odd widths and seam isolation.
    for(auto mode:{OutputMode::HalfSBS,OutputMode::FullSBS}) {
      auto pair=pack_sbs(l,rr,mode);
      for(auto fit:{CanvasFit::Fit,CanvasFit::Fill}) {
        for(int w:{3,4,8,17,32}) for(int h:{1,2,8}) {
          auto canvas=fit_sbs_canvas(pair,w,h,mode,2.0,fit);
          check(canvas.width==w && canvas.height==h,"canvas dimensions");
          for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            auto p=canvas.at(x,y);near(p.a,1,"canvas opaque coverage");
            near(x<w/2?p.g:p.r,0,"no cross-eye interpolation");
            if(fit==CanvasFit::Fill)near(p.r+p.g,1,"fill has no bars");
          }
        }
      }
      const int w=mode==OutputMode::HalfSBS?8:16;
      auto bars=fit_sbs_canvas(pair,w,8,mode,2.0,CanvasFit::Fit);
      near(bars.at(w/4,0).r,0,"fit keeps top bar");
      near(bars.at(w/4,3).r,1,"fit centered first eye");
      near(bars.at(3*w/4,3).g,1,"fit centered second eye");
      auto wide=fit_sbs_canvas(pair,w*2,2,mode,2.0,CanvasFit::Fit);
      near(wide.at(0,0).r,0,"fit keeps side bar");
    }
    Image gradient(8,2);
    for(int y=0;y<2;++y)for(int x=0;x<8;++x)gradient.at(x,y)={float(x),-.3f,2.2f,.5f};
    auto filled=fit_canvas(gradient,2,2,CanvasFit::Fill);
    near(filled.at(0,0).r,3,"fill crops centered left edge");
    near(filled.at(1,0).r,4,"fill crops centered right edge");
    near(filled.at(0,0).g,-.3f,"fit retains negative colors");
    near(filled.at(0,0).b,2.2f,"fit retains HDR colors");
    near(filled.at(0,0).a,.5f,"fit retains source alpha");
    rejects([&]{fit_sbs_canvas(packed,1,2,OutputMode::FullSBS,2,CanvasFit::Fit);},"one-pixel SBS canvas rejected");
    Image padded(16,12,Pixel{5,6,7,0}); // Hidden RGB is not picture content.
    for(int y=3;y<9;++y)for(int x=4;x<12;++x)padded.at(x,y)={0,0,0,1};
    padded.at(5,4)={-.3f,2.2f,.7f,.25f};
    auto unpadded=trim_transparent_canvas(padded);
    check(unpadded.width==8 && unpadded.height==6,"remove only transparent outer margins");
    near(unpadded.at(0,0).a,1,"opaque black retained at picture edge");
    near(unpadded.at(1,1).r,-.3f,"unpadded float color preserved");
    near(unpadded.at(1,1).g,2.2f,"unpadded HDR preserved");
    near(unpadded.at(1,1).a,.25f,"partial alpha preserved");
    auto empty=trim_transparent_canvas(Image(16,12));
    check(empty.width==16 && empty.height==12,"empty input keeps valid dimensions");
    auto black_picture=trim_transparent_canvas(Image(16,12,Pixel{0,0,0,1}));
    check(black_picture.width==16 && black_picture.height==12,"black imagery is never trimmed");
    Image edge_alpha(8,6);edge_alpha.at(0,0).a=.00001f;edge_alpha.at(7,5).a=.00001f;
    auto preserved=trim_transparent_canvas(edge_alpha);
    check(preserved.width==8 && preserved.height==6,"faint edge content is retained");
    // Occluding near sample must win, even when a far sample lands afterward.
    Image colors(100,1,Pixel{0,1,0,1});Depth z(100,1,0);
    colors.at(50,0)={1,0,0,1};z.at(50,0)=1;
    auto occluded=reproject(colors,z,stereo,true);near(occluded.image.at(52,0).r,1,"z buffer near wins");
    std::cout<<checks<<" checks passed\n";return 0;
  }catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
