#pragma once
#include "Hitable.h"
#include "BVH.h"
#include "Sphere.h"
#include <curand_discrete2.h>
#include <device_launch_parameters.h>
#include <curand_kernel.h>
#include "Camera.h"

#define  RenderDEBUG false
#define  DebugLog false


__global__
void IPRSampler(int d_width, int d_height, int seed, int SPP, int MST, int root, float * d_pixeldata, Camera* d_camera, curandState *const rngStates,DeviceData data)
{
	const auto tid = blockIdx.x * blockDim.x + threadIdx.x;
	const auto tid2 = blockIdx.y * blockDim.y + threadIdx.y;

	curand_init(seed + tid + tid2 * d_width, 0, 0, &rngStates[tid]);			//初始化随机数
	Vec3 color(0, 0, 0);

#if !RenderDEBUG 
	const int x = blockIdx.x * 16 + threadIdx.x, y = blockIdx.y * 16 + threadIdx.y;
#endif
	unsigned int stack[16];
	for (int i = 0; i < 16; i++)stack[i] = 0;
#if RenderDEBUG 
	int x = 256, y = 256;
	float u = float(x) / float(512);
	float v = float(y) / float(512);
#endif
	for (auto j = 0; j < SPP; j++)
	{
#if !RenderDEBUG 
		const auto u = float(x + curand_uniform(&rngStates[tid])) / float(d_width), v = float(y + curand_uniform(&rngStates[tid])) / float(d_height);
#endif
		Ray ray(d_camera->Origin(), d_camera->LowerLeftCorner() + u * d_camera->Horizontal() + v * d_camera->Vertical() - d_camera->Origin());
		Vec3 c(0, 0, 0);
		Vec3 factor(1, 1, 1);
		for (auto i = 0; i < MST; i++)
		{
			// Hitable** stackPtr = new Hitable*[5];
			// *stackPtr++ = nullptr; // push

			if (DebugLog)printf("采样开始\n");
			auto stack_ptr = 0;

			HitRecord rec;
			rec.t = 99999;
			//bool hit;
			//抛弃递归二分查找而是使用循环二分查找 以降低栈深度
			auto node = data.world[root];
			do
			{
				//printf("循环开始\n");
				HitRecord recl, recr;
				//printf("node type: %d\n",node->type);
				auto bvh = static_cast<BVHNode*>(node);
				const auto childL = data.world[bvh->left_id];
				const auto childR = data.world[bvh->right_id];
				
				bool overlarpR, overlarpL;
				const auto left_is_bvh = childL->type == Instance::BVH;
				const auto right_is_bvh = childR->type == Instance::BVH;
		
				if (left_is_bvh)
				{
					const auto child_bvh_L = static_cast<BVHNode*>(childL);
					overlarpL = child_bvh_L->Box.Hit(ray, 0.001, 99999);
				}
				else
				{
					overlarpL = childL->Hit(ray, 0.001, 99999, recl, &data);
				}

		
				if (right_is_bvh)
				{
					const auto child_bvh_R = static_cast<BVHNode*>(childR);
					overlarpR = child_bvh_R->Box.Hit(ray, 0.001, 99999);
				}
				else
				{
					overlarpR = childR->Hit(ray, 0.001, 99999, recr, &data);
				}
	
				if (overlarpL&&!left_is_bvh)
				{
					
					if (recl.t < rec.t) {
						//hit = true;

						rec = HitRecord(&recl);
						//printf("Set Left  lefttype: %d  t:%f < %f? \n", childL->type, recl.t, rec.t);
						//printf("Record after set   t:%f \n", rec.t);
					}
				}

				if (overlarpR&&!right_is_bvh)
				{
					
					//printf("Set Right\n");
					if (recr.t < rec.t) {
						//hit = true;
						rec = HitRecord(&recr);
					}
				}
				//printf("state 4\n");
				const bool traverseL = (overlarpL && left_is_bvh);
				const bool traverseR = (overlarpR && right_is_bvh);

				if (DebugLog) printf("node id: %d  left: %d, right: %d, hit_left:%s hit_right:%s, lt: %f rt: %f, l_is_bvh:%s r_is_bvh:%s\n",
					node->id, bvh->left_id, bvh->right_id, 
					overlarpL ? "true" : "false", overlarpR ? "true" : "false",
					recl.t,recr.t,
					left_is_bvh ? "true" : "false",right_is_bvh? "true" : "false"
					);

				//printf("?? %d:%d\n",stack_ptr,stack[0]);
				if (!traverseL && !traverseR) {
					if (stack_ptr <= 0)
					{
						if(DebugLog)printf("break with ptr = %d \n",stack_ptr);
						node = nullptr;
					}
					else {
						//printf("set node %d\n", stack_ptr - 1);
						node = data.world[stack[--stack_ptr]];
						//printf("设置完成 此处没有越界\n", stack_ptr - 1);
					}
					//node = *--stackPtr; // pop
				}
				else
				{
					node = (traverseL) ? childL : childR;
					if (traverseL && traverseR) {
						if (DebugLog)printf("Push %d\n", childR->id);
						stack[stack_ptr++] = bvh->right_id;
					}
						//*stackPtr++ = childR; // push
				}

			} while (node != nullptr);
			//printf("结束循环\n");
			//free(stackPtr);
			// free(stack);
			//printf(" t: %f\n", rec.t);
			//if (d_world[root]->Hit(ray, 0.001, 99999, rec, materials, d_world))
			if (rec.t<99998)
			{
				//printf(" hit");

				// random in unit Sphere
				Vec3 random_in_unit_sphere;
				do random_in_unit_sphere = 2.0*Vec3(curand_uniform(&rngStates[tid]), curand_uniform(&rngStates[tid]), curand_uniform(&rngStates[tid])) - Vec3(1, 1, 1);
				while (random_in_unit_sphere.squared_length() >= 1.0);

				Ray scattered;
				Vec3 attenuation;
				if (i < MST&&rec.mat_ptr->scatter(ray, rec, attenuation, scattered, random_in_unit_sphere, curand_uniform(&rngStates[tid])))
				{
					factor *= attenuation;
					ray = scattered;
				}
					//****** 超过最大反射次数，返回黑色 ******
				else
				{
					c = Vec3(0, 0, 0);
					break;
				}
			}
			else
			{
				//printf("SetBG");
				const auto t = 0.5*(unit_vector(ray.Direction()).y() + 1);
				//c = factor * ((1.0 - t)*Vec3(1.0, 1.0, 1.0) + t * Vec3(0.5, 0.7, 1.0));
				c = factor * ((1.0 - t)*Vec3(1.0, 1.0, 1.0) + t * Vec3(50/255.0, 130/255.0, 255/255.0));
				break;
			}
		}
		color += c;
	}
	//color /= SPP;

	//SetColor
	const auto i = d_width * 4 * y + x * 4;
	d_pixeldata[i] += color.r();
	d_pixeldata[i + 1] += color.g();
	d_pixeldata[i + 2] += color.b();
	d_pixeldata[i + 3] += SPP;
}

__global__ inline void WorldArrayFixer(Hitable** d_world, Hitable** new_world)//,int rootid,BVHNode* root)//
{
	const auto i = threadIdx.x;//TODO add dim ,1024 is not enough
	switch (d_world[i]->type)
	{
	case 1:
		new_world[i] = new Sphere(d_world[i]);
		break;
	case 2:
		new_world[i] = new BVHNode(d_world[i]);
		break;
	default:;
	}
}

__global__ inline void ArraySetter(Hitable** location, int i, Hitable* obj)//
{
	location[i] = obj;
}

// __global__ inline void TextureLab(float * d_pixeldata)//
// {
// 	const int x = blockIdx.x * 16 + threadIdx.x, y = blockIdx.y * 16 + threadIdx.y;
// 	auto tex2D(Renderer::tex, x/512, y);
// }