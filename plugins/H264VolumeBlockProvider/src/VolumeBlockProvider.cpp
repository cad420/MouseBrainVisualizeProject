//
// Created by wyz on 2022/2/24.
//
#include "VolumeBlockProvider.hpp"
#include "algorithm/GPUTaskScheduleHelper.hpp"
#include "common/LRU.hpp"
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}
MRAYNS_BEGIN

struct H264VolumeBlockProvider::Impl{

    Impl(){
        packet_cache = std::make_unique<PacketCacheType>(PacketCacheNum);
    }
    static constexpr int PacketCacheNum = 1024;
    using PacketType = std::vector<std::vector<uint8_t>>;
    using PacketCacheType = LRUCache<BlockIndex,PacketType>;
    class PacketReader{
        int min_lod{0},max_lod{0};
        Volume volume;
        std::unordered_map<int,std::unique_ptr<Reader>> lod_readers;
      public:
        void open(const std::string& filename){
            if(filename.empty()) return;
            LodFile lod_file;
            lod_file.open_lod_file(filename);
            min_lod = lod_file.get_min_lod();
            max_lod = lod_file.get_max_lod();
            for(int i =min_lod;i<=max_lod;i++){
                lod_readers[i] = std::make_unique<Reader>(lod_file.get_lod_file_path(i).c_str());
                lod_readers[i]->read_header();
            }

            sv::Header header{};
            lod_readers[min_lod]->read_header(header);
            volume.padding = header.padding;
            volume.block_length = std::pow(2,header.log_block_length);
            volume.name = "mouse";
            volume.volume_dim_x = header.raw_x;
            volume.volume_dim_y = header.raw_y;
            volume.volume_dim_z = header.raw_z;
            auto space = lod_file.get_volume_space();
            volume.volume_space_x = space[0];
            volume.volume_space_y = space[1];
            volume.volume_space_z = space[2];
            volume.voxel_type = Volume::UINT8;
            volume.max_lod = max_lod;
            assert(volume.isValid());

        }
        void readPacket(BlockIndex index,PacketType& packets){
            int lod = index.w;
            assert(lod>=min_lod && lod<=max_lod);
            lod_readers.at(lod)->read_packet({uint32_t(index.x),uint32_t(index.y),uint32_t(index.z)},packets);
        }
        Volume getVolume(){
            return volume;
        }
    };
    std::unique_ptr<PacketReader> packet_reader;
    std::unique_ptr<PacketCacheType> packet_cache;
    std::mutex cache_mtx;
    void openPacketReader(const std::string& filename){
        packet_reader = std::make_unique<PacketReader>();
        packet_reader->open(filename);
    }
    void readPacket(BlockIndex index,PacketType& packets){
        std::lock_guard<std::mutex> lk(cache_mtx);
        auto p = packet_cache->get_value_ptr(index);
        if(p){
            packets = *p;
            return ;
        }
        packet_reader->readPacket(index,packets);

        packet_cache->emplace_back(index,packets);

    }
    Volume getVolume(){
        return packet_reader->getVolume();
    }
    /**
     *
     */
//    class Decoder{
//        /**
//         * 一个worker一次只能处理一个任务 因为同时处理 即多线程处理不会加快效率
//         */
//        std::unique_ptr<VoxelUncompress> worker;
//        std::mutex mtx;//access only in single-thread context
//      public:
//        void create(int GPUIndex){
//            VoxelUncompressOptions opts;
//            opts.device_id = GPUIndex;
//            worker = std::make_unique<VoxelUncompress>(opts);
//        }
//        void decode(void* ptr,size_t size,PacketType& packets){
//            std::lock_guard<std::mutex> lk(mtx);
//            worker->uncompress(reinterpret_cast<uint8_t*>(ptr),size,packets);
//        }
//    };
    struct Decoder{
        size_t decode(AVCodecContext* c,AVFrame* frame,AVPacket* pkt,uint8_t* buf){
            int ret = avcodec_send_packet(c,pkt);
            if(ret < 0){
                throw std::runtime_error("error sending a packet for decoding");
            }
            size_t frame_pos = 0;
            while(ret >= 0){
                ret = avcodec_receive_frame(c,frame);
                if(ret == AVERROR(EAGAIN) || ret ==AVERROR_EOF)
                    break;
                else if(ret < 0){
                    throw std::runtime_error("error during decoding");
                }
                memcpy(buf + frame_pos,frame->data[0],frame->linesize[0]*frame->height);
                frame_pos += frame->linesize[0] * frame->height;
            }
            return frame_pos;
        }

        void uncompress(void* data,size_t len,std::vector<std::vector<uint8_t>>& packets){
            auto codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
            assert(codec);
//        auto parser = av_parser_init(codec->id);
//        assert(parser);

            auto c = avcodec_alloc_context3(codec);
            c->thread_count = 6;
            c->delay = 0;
            assert(c);
            int ret = avcodec_open2(c,codec,nullptr);
            assert(ret >= 0);
            auto frame = av_frame_alloc();
            assert(frame);
            auto pkt = av_packet_alloc();
            assert(pkt);
            uint8_t* p = (uint8*)data;
            size_t offset = 0;

            for(auto& packet:packets){
                pkt->data = packet.data();
                pkt->size = packet.size();
                offset += decode(c,frame,pkt,p+offset);
                if(offset > len){
                    throw std::runtime_error("decode result out of buffer range");
                }
            }
            decode(c,frame,nullptr,p+offset);

            avcodec_free_context(&c);
            av_frame_free(&frame);
            av_packet_free(&pkt);
        }
    };

//    std::vector<Worker> workers;
    std::unique_ptr<Decoder[]> decoders{nullptr};
    int worker_count{0};

    void createDecoders(int n){
        worker_count = n;
        decoders = std::make_unique<Decoder[]>(n);
//        for(int i = 0;i<n;i++){
//            decoders[i].create(i);
//        }
    }
    void decode(int GPUIndex,void* ptr,size_t size,PacketType& packets){
        if(GPUIndex>=worker_count){
            throw std::runtime_error("GPU index out of range");
        }
        decoders[GPUIndex].uncompress(ptr,size,packets);
    }
};

void H264VolumeBlockProvider::open(const std::string &filename)
{
    impl->openPacketReader(filename);

    volume = impl->getVolume();


}
void H264VolumeBlockProvider::setHostNode(HostNode* hostNode)
{
    assert(hostNode);
    this->host_node = hostNode;

    impl->createDecoders(this->host_node->getGPUNum());
}
const Volume& H264VolumeBlockProvider::getVolume() const
{
    return this->volume;
}
void H264VolumeBlockProvider::getVolumeBlock(void *dst,BlockIndex blockIndex)
{
    Impl::PacketType packets;

    impl->readPacket(blockIndex,packets);

    int gpu_index = GPUTaskScheduleHelper::GetOptimalGPUIndex(host_node,GPUTaskScheduleHelper::DefaultGPUTaskCodecEvaluator);

    auto task_handle = host_node->recordGPUTask(
        {HostNode::Codec,1,1,gpu_index}
        );

    impl->decode(gpu_index,dst,volume.getBlockSize(),packets);

    host_node->eraseGPUTask(task_handle);
}
H264VolumeBlockProvider::H264VolumeBlockProvider()
{
    impl = std::make_unique<Impl>();
}

MRAYNS_END

REGISTER_PLUGIN_FACTORY_IMPL(H264VolumeBlockProviderFactory)
EXPORT_PLUGIN_FACTORY_IMPL(H264VolumeBlockProviderFactory)

