// Produce a separate face-only dataset. Original images are never changed.
#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <cstdio>
#include <fstream>
#include <string>
#include "auth_pipeline.h"
#include "fs_compat.h"
#include "image_io.h"

static std::string csv(const std::string& value) {
    std::string out="\"";
    for(char c:value) {if(c=='\"')out+='\"';out+=c;}
    return out+'\"';
}
int main(int argc,char** argv) {
    using namespace fa;
    if(argc<3 || argc>4) {std::printf("mask_portraits <source_folder> <NEW_output_folder> [profile.ini]\n");return 2;}
    try {
        const fs::path source=fs::canonical(fs::path(argv[1]));
        const fs::path target=fs::absolute(fs::path(argv[2]));
        if(!fs::is_directory(source)||fs::exists(target)) {
            std::fprintf(stderr,"Source must be a directory; output must not exist. Originals are never overwritten.\n");return 2;
        }
        std::vector<fs::path> files;
        for(const auto& entry:fs::recursive_directory_iterator(source)) {
            std::string ext=entry.path().extension().string();
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return (char)std::tolower(c);});
            if(ext==".jpg"||ext==".jpeg"||ext==".png"||ext==".bmp")files.push_back(entry.path());
        }
        std::sort(files.begin(),files.end());
        AuthPipeline pipeline;ModelProfile profile;
        if(!load_profile(argc==4?argv[3]:"models/permissive.ini",".",profile)||!pipeline.init(profile,""))return 1;
        fs::create_directories(target);
        std::ofstream manifest((target/"MASK_MANIFEST.csv").string());
        manifest<<"source,output,status,reason,original_similarity\n";
        int kept=0,rejected=0;
        for(const auto& file:files) {
            std::string reason;Image input,masked;Face face;Feature original,derived;
            float similarity=kNoScore;
            std::vector<Face> faces;
            if(!load_image(file.string(),input))reason="unreadable image";
            else if(!pipeline.detector().detect(input,faces)||faces.size()!=1)reason="requires exactly one detected face";
            else if(!faces[0].has_landmarks || faces[0].box.w<64 || faces[0].box.h<64)reason="face too small or missing landmarks";
            else if(!pipeline.extract_from_image(input,original,&face))reason="original face feature unavailable";
            if(reason.empty()) {
                // Keep only pixels within a tight ellipse inside the face bounding box.
                // All other content (hair margins, shoulders, clothing, backdrop) is neutral.
                const Rect box=face.box;
                int width=(int)std::ceil(box.w*1.3f),height=(int)std::ceil(box.h*1.3f);
                masked.create(width,height,3);std::fill(masked.data.begin(),masked.data.end(),240);
                int left=(int)std::floor(box.x+box.w*.5f-width*.5f);
                int top=(int)std::floor(box.y+box.h*.5f-height*.5f);
                double cx=box.x+box.w*.5,cy=box.y+box.h*.5;
                for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                    int sx=left+x,sy=top+y;
                    double nx=(sx-cx)/(box.w*.48),ny=(sy-cy)/(box.h*.49);
                    if(nx*nx+ny*ny<=1 && sx>=0&&sy>=0&&sx<input.width&&sy<input.height)
                        std::copy(input.row(sy)+sx*3,input.row(sy)+sx*3+3,masked.row(y)+x*3);
                }
                faces.clear();
                if(!pipeline.detector().detect(masked,faces)||faces.size()!=1 || !pipeline.extract_from_image(masked,derived))
                    reason="masked face cannot be detected or extracted";
                else {
                    similarity=0;
                    for(int i=0;i<original.dim();++i)similarity+=original.values[i]*derived.values[i];
                    if(similarity<pipeline.match_threshold())reason="masked feature below original identity threshold";
                }
            }
            // Preserve each person's relative folder and the complete source filename.
            std::string relative=file.generic_string().substr(source.generic_string().size()+1);
            fs::path output=target/fs::path(relative+".png");
            if(reason.empty()) {
                fs::create_directories(output.parent_path());
                if(!save_image(output.string(),masked))reason="cannot write masked image";
            }
            bool accepted=reason.empty();if(accepted)++kept;else ++rejected;
            manifest<<csv(file.generic_string())<<','<<csv(accepted?output.generic_string():"")<<','
                    <<(accepted?"kept":"excluded")<<','<<csv(reason)<<','<<similarity<<'\n';
            if(!manifest)throw std::runtime_error("Cannot write mask manifest.");
            std::printf("%s %s%s%s\n",accepted?"kept":"excluded",relative.c_str(),accepted?"":": ",reason.c_str());
        }
        if(fs::exists(source/"LICENSES.csv")) {
            std::ifstream attribution((source/"LICENSES.csv").string(),std::ios::binary);
            std::ofstream copy((target/"SOURCE_LICENSES.csv").string(),std::ios::binary);
            copy<<attribution.rdbuf();
            if(!copy)throw std::runtime_error("Cannot preserve source attribution.");
        }
        std::ofstream note((target/"README.txt").string());
        note<<"Face-only derivatives. Originals remain in "<<source.generic_string()<<".\n"
            <<"Only pixels inside the detected face ellipse are retained; surrounding pixels are RGB 240.\n"
            <<"No generative edits, identity changes, nationality or cultural-membership filtering.\n"
            <<"MASK_MANIFEST.csv records each source, output, exclusion reason and identity similarity.\n"
            <<"Source licenses and attribution remain in the original dataset LICENSES.csv.\n"
            <<"Background masking is geometric, not semantic symbol recognition. Review retained faces for any marks within the face itself.\n";
        std::printf("SUMMARY: %d kept, %d excluded, %zu originals untouched\n",kept,rejected,files.size());
        return kept>0?0:1;
    } catch(const std::exception& e) {std::fprintf(stderr,"mask_portraits: %s\n",e.what());return 1;}
}
