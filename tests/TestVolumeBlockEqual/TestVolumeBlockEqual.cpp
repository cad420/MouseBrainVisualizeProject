//
// Created by wyz on 2022/2/24.
//
#include <fstream>
#include <iostream>
#include <vector>
#include <string>

void LoadVolume(const std::string& filename,std::vector<uint8_t>& data){
    size_t volumeSize = 0;
    {
        auto _pos = filename.find_last_of('_');
        auto ext = filename.substr(_pos+1);
        if(ext != "uint8.raw"){
            throw std::runtime_error("error volume file type or format");
        }
        auto t = filename.substr(0,_pos);
        auto zpos = t.find_last_of('_');
        auto z = std::stoi(t.substr(zpos+1));
        t = t.substr(0,zpos);
        auto ypos = t.find_last_of('_');
        auto y = std::stoi(t.substr(ypos+1));
        t = t.substr(0,ypos);
        auto xpos = t.find_last_of('_');
        auto x = std::stoi(t.substr(xpos+1));
        auto name = t.substr(0,xpos);
//        LOG_INFO("volume name: {0}, dim: {1} {2} {3}, ext: {4}",name,x,y,z,ext);
        volumeSize = (size_t) x * y * z;
    }
    std::ifstream in(filename,std::ios::binary|std::ios::ate);
    if(!in.is_open()){
        throw std::runtime_error("open volume file failed");
    }
    auto fileSize = in.tellg();
    if(fileSize != volumeSize){
        throw std::runtime_error("invalid volume file size");
    }
    in.seekg(0,std::ios::beg);
    data.resize(volumeSize);
    in.read(reinterpret_cast<char*>(data.data()),volumeSize);
    in.close();
}
int main(){
    std::string path1 = "C:\\Users\\wyz\\projects\\MouseBrainVisualizeProject\\tests\\data\\gen#1#2#0#4_512_512_512_uint8.raw";
    std::string path2 = "C:\\Users\\wyz\\projects\\MouseBrainVisualizeProject\\tests\\data\\1#2#0#lod4_512_512_512_uint8.raw";

    std::vector<uint8_t> volume1;
    std::vector<uint8_t> volume2;
    try{
        LoadVolume(path1, volume1);
        LoadVolume(path2, volume2);
    }
    catch (const std::exception& err)
    {
        std::cout<<err.what()<<std::endl;
    }
    if(volume1.size()!=volume2.size()){
        std::cout<<"volume size not equal"<<std::endl;
    }
    else{
        size_t not_equal_count = 0;
        for(size_t i = 0; i < volume1.size(); i++){
            if(volume1[i]!=volume2[i]){
                not_equal_count++;
            }
        }
        std::cout<<"not equal count: "<<not_equal_count<<std::endl;
        std::vector<int> table(256,0);
        for(auto x : volume1){
            table[x]++;
        }
        for(int i = 0;i<256;i++){
            std::cout<<i<<","<<table[i]<<" ";
        }
    }
    return 0;
}