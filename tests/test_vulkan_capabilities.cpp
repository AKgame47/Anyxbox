// Actual production render rejection paths with injected query results, not physical-device evidence.
#include <cstdio>
#include <cstring>
#include <vector>
#include "gfx/vulkan.hpp"
#include "dxbc_builder.h"
#include "shader/dxbc_spirv.h"
#define CHECK(c) do{if(!(c)){std::printf("FAIL line %d: %s\n",__LINE__,#c);return 1;}}while(0)
namespace pnr::graphics {
struct VulkanCapabilityTestAccess {
 static void*& query(VulkanBackend& v){return v.vkGetPhysicalDeviceImageFormatProperties;}
 static void*& create(VulkanBackend& v){return v.vkCreateImage;}
};
}
using namespace pnr::graphics;
static int branch=0,calls=0,reject_call=1,sampled_images=0;
static void* real_create=nullptr;
static VkResult query(VkPhysicalDevice,int32_t format,int32_t type,int32_t tiling,VkFlags usage,VkFlags flags,VkImageFormatProperties* cap){
 ++calls;
 if(format!=VK_FORMAT_R8G8B8A8_UNORM || type!=VK_IMAGE_TYPE_2D || tiling!=VK_IMAGE_TILING_OPTIMAL ||
    usage!=(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT) || flags!=0) return -11;
 *cap={};cap->maxExtent={1024,1024,1};cap->maxMipLevels=11;cap->maxArrayLayers=256;cap->sampleCounts=VK_SAMPLE_COUNT_1_BIT;cap->maxResourceSize=64u*1024u*1024u;
 if(calls!=reject_call)return VK_SUCCESS;
 switch(branch){case 0:return -11;case 1:cap->maxExtent.width=1;break;case 2:cap->maxExtent.height=1;break;
 case 3:cap->maxExtent.depth=0;break;case 4:cap->maxMipLevels=0;break;case 5:cap->maxArrayLayers=0;break;
 case 6:cap->sampleCounts=0;break;case 7:cap->maxResourceSize=1;break;}
 return VK_SUCCESS;
}
static VkResult create(VkDevice d,const VkImageCreateInfo* i,void* a,VkImage* image){
 if(i->usage&VK_IMAGE_USAGE_SAMPLED_BIT)++sampled_images;
 return reinterpret_cast<VkResult(*)(VkDevice,const VkImageCreateInfo*,void*,VkImage*)>(real_create)(d,i,a,image);
}
int main(int argc,char** argv){
 VulkanBackend vk;bool required=argc>2 && std::strcmp(argv[2],"--require-vulkan")==0;
 if(!vk.open() || !vk.create_instance("capability-rejection") || vk.devices().empty() || !vk.create_device(0)){
  CHECK(!required);std::puts("SKIP injected capability render checks: no Vulkan device");return 0;
 }
 auto vb=dxb::vs_tex_pass(),pb=dxb::ps_sample();
 auto vs=anyxbox::shader::dxbc_to_spirv(vb.data(),vb.size()),ps=anyxbox::shader::dxbc_to_spirv(pb.data(),pb.size());CHECK(vs.ok&&ps.ok);
 std::vector<float> vertices={-1,1,0,1,1,1,1,0,0, 1,1,0,1,1,1,1,1,0, -1,-1,0,1,1,1,1,0,1};
 std::vector<uint32_t> idx={0,1,2};std::vector<uint8_t> output;uint8_t bytes[16]={};
 TexInput input;input.rgba=bytes;input.w=2;input.h=2;input.byte_size=sizeof bytes;
 const float clear[4]={};
 void* original=VulkanCapabilityTestAccess::query(vk);real_create=VulkanCapabilityTestAccess::create(vk);
 struct Restore{VulkanBackend& v;void* q;~Restore(){VulkanCapabilityTestAccess::query(v)=q;VulkanCapabilityTestAccess::create(v)=real_create;}} restore{vk,original};
 VulkanCapabilityTestAccess::query(vk)=reinterpret_cast<void*>(&query);VulkanCapabilityTestAccess::create(vk)=reinterpret_cast<void*>(&create);
 const char* reasons[]={"image-format query failed","width","height","depth","mip count","array layers","sample count","resource bytes"};
 for(int extra=0;extra<2;++extra)for(branch=0;branch<8;++branch){
  calls=sampled_images=0;reject_call=extra?2:1;std::vector<TexInput> extras={input};
  auto s=vk.render_textured_clip_mesh_rgba(8,8,vs.words,ps.words,vertices,idx,false,input,clear,&output,nullptr,true,nullptr,VK_CULL_MODE_NONE,VK_FRONT_FACE_COUNTER_CLOCKWISE,VK_COMPARE_OP_LESS,nullptr,nullptr,nullptr,extra?&extras:nullptr);
  CHECK(!s && s.error.message==std::string("sampled texture device capability: ")+reasons[branch]);
  CHECK(calls==reject_call && sampled_images==extra);
  std::printf("injected %s rejection: %s; rejected image not created\n",extra?"t1":"t0",reasons[branch]);
 }
 // Restore real query before proving the device path still works.
 VulkanCapabilityTestAccess::query(vk)=original;sampled_images=0;
 CHECK(vk.render_textured_clip_mesh_rgba(8,8,vs.words,ps.words,vertices,idx,false,input,clear,&output));
 CHECK(sampled_images==1 && output.size()==8*8*4);
 std::puts("real driver query restored: render passed");
 return 0;
}
