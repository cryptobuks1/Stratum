#pragma once
#include <Content/Mesh.hpp>
#include <Core/Buffer.hpp>
#include <Core/Framebuffer.hpp>
#include <Core/DescriptorSet.hpp>
#include <Core/RenderPass.hpp>
#include <Core/Window.hpp>
#include <Scene/Object.hpp>
#include <Util/Util.hpp>

class Camera : public virtual Object {
public:
	ENGINE_EXPORT Camera(const std::string& name, Window* targetWindow, VkFormat depthFormat = VK_FORMAT_D32_SFLOAT, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_8_BIT, bool renderDepthNormals = true);
	ENGINE_EXPORT Camera(const std::string& name, ::Device* device, VkFormat renderFormat = VK_FORMAT_R8G8B8A8_UNORM, VkFormat depthFormat = VK_FORMAT_D32_SFLOAT, VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_8_BIT, bool renderDepthNormals = true);
	ENGINE_EXPORT Camera(const std::string& name, ::Framebuffer* framebuffer);
	ENGINE_EXPORT virtual ~Camera();

	inline ::Device* Device() const { return mDevice; }

	ENGINE_EXPORT virtual void PreRender();
	ENGINE_EXPORT virtual void Set(CommandBuffer* commandBuffer);

	ENGINE_EXPORT virtual float4 WorldToClip(const float3& worldPos);
	ENGINE_EXPORT virtual float3 ClipToWorld(const float3& clipPos);
	ENGINE_EXPORT virtual Ray ScreenToWorldRay(const float2& uv);

	inline virtual uint32_t RenderPriority() const { return mRenderPriority; }
	inline virtual void RenderPriority(uint32_t x) { mRenderPriority = x; }

	inline Window* TargetWindow() const { return mTargetWindow; }

	// If TargetWindow is nullptr and SampleCount is not VK_SAMPLE_COUNT_1, resolves the framebuffer to ResolveBuffer and transitions ResolveBuffer to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	// If TargetWindow is nullptr and SampleCount is VK_SAMPLE_COUNT_1, transitions the framebuffer to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
	// If TargetWindow is not nullptr, resolves or copies the framebuffer to the window
	ENGINE_EXPORT virtual void Resolve(CommandBuffer* commandBuffer);

	inline virtual bool Orthographic() const { return mOrthographic; }
	inline virtual float OrthographicSize() const { return mOrthographicSize; }
	inline virtual float Aspect() const { return mViewport.width / mViewport.height; }
	inline virtual float Near() const { return mNear; }
	inline virtual float Far() const { return mFar; }
	inline virtual float FieldOfView() const { return mFieldOfView; }
	inline virtual float2 PerspectiveSize() const { return mPerspectiveSize; }
	inline virtual float ViewportX() const { return mViewport.x; }
	inline virtual float ViewportY() const { return mViewport.y; }
	inline virtual float ViewportWidth() const { return mViewport.width; }
	inline virtual float ViewportHeight() const { return mViewport.height; }
	inline virtual uint32_t FramebufferWidth()  const { return mFramebuffer->Width();  }
	inline virtual uint32_t FramebufferHeight() const { return mFramebuffer->Height(); }
	inline virtual VkSampleCountFlagBits SampleCount() const { return mFramebuffer->SampleCount(); }

	inline virtual void Orthographic(bool o) { mOrthographic = o; mMatricesDirty = true; }
	inline virtual void OrthographicSize(float s) { mOrthographicSize = s; mMatricesDirty = true; }
	inline virtual void Near(float n) { mNear = n; mMatricesDirty = true; }
	inline virtual void Far(float f) { mFar = f;  mMatricesDirty = true; }
	inline virtual void FieldOfView(float f) { mPerspectiveSize = 0; mFieldOfView = f; mMatricesDirty = true; }
	inline virtual void ViewportX(float x) { mViewport.x = x; }
	inline virtual void ViewportY(float y) { mViewport.y = y; }
	inline virtual void ViewportWidth(float f) { mViewport.width = f; mMatricesDirty = true; }
	inline virtual void ViewportHeight(float f) { mViewport.height = f; mMatricesDirty = true; }
	inline virtual void PerspectiveSize(const float2& p) { mPerspectiveSize = p; mFieldOfView = 0; mMatricesDirty = true; }
	inline virtual void FramebufferWidth (uint32_t w) { mFramebuffer->Width(w);  mMatricesDirty = true; }
	inline virtual void FramebufferHeight(uint32_t h) { mFramebuffer->Height(h); mMatricesDirty = true; }
	inline virtual void SampleCount(VkSampleCountFlagBits s) { mFramebuffer->SampleCount(s); }

	inline virtual ::Framebuffer* Framebuffer() const { return mFramebuffer; }
	inline virtual Texture* ColorBuffer(uint32_t index = 0) const { return mFramebuffer->ColorBuffer(index); }
	inline virtual Texture* ResolveBuffer(uint32_t index = 0) const { return mFramebuffer->SampleCount() == VK_SAMPLE_COUNT_1_BIT ? mFramebuffer->ColorBuffer(index) : mResolveBuffers[mDevice->FrameContextIndex()][index]; }

	inline virtual Buffer* UniformBuffer() const { return mUniformBuffer; }
	ENGINE_EXPORT virtual ::DescriptorSet* DescriptorSet(VkShaderStageFlags stage);

	/// Note: The view matrix is calculated placing the camera at the origin. To transform from world->view, one must apply:
	/// view * (worldPos-cameraPos)
	inline virtual float4x4 View() { UpdateMatrices(); return mView; }
	inline virtual float4x4 Projection() { UpdateMatrices(); return mProjection; }
	inline virtual float4x4 ViewProjection() { UpdateMatrices(); return mViewProjection; }
	inline virtual float4x4 InverseView() { UpdateMatrices(); return mInvView; }
	inline virtual float4x4 InverseProjection() { UpdateMatrices(); return mInvProjection; }
	inline virtual float4x4 InverseViewProjection() { UpdateMatrices(); return mInvViewProjection; }

	ENGINE_EXPORT virtual bool IntersectFrustum(const AABB& aabb);

	ENGINE_EXPORT virtual void DrawGizmos(CommandBuffer* commandBuffer, Camera* camera);

private:
	uint32_t mRenderPriority;

	bool mRenderDepthNormals;

	bool mOrthographic;
	float mOrthographicSize;

	float mFieldOfView;
	float mNear;
	float mFar;
	float2 mPerspectiveSize;

	float4x4 mView;
	float4x4 mProjection;
	float4x4 mViewProjection;
	float4x4 mInvView;
	float4x4 mInvProjection;
	float4x4 mInvViewProjection;
	bool mMatricesDirty;

	// Frustum planes
	float4 mFrustum[6];

	VkViewport mViewport;

	Window* mTargetWindow;
	::Device* mDevice;
	::Framebuffer* mFramebuffer;
	bool mDeleteFramebuffer;

	// Resolve buffers, if the camera does not have a TargetWindow
	std::vector<Texture*>* mResolveBuffers;

	void** mUniformBufferPtrs;
	Buffer* mUniformBuffer;
	std::vector<std::unordered_map<VkShaderStageFlags, ::DescriptorSet*>> mDescriptorSets;

	void CreateDescriptorSet();

protected:
	ENGINE_EXPORT virtual void Dirty();
	ENGINE_EXPORT virtual bool UpdateMatrices();
};