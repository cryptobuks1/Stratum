#include <Scene/Environment.hpp>

#include <Core/Buffer.hpp>
#include <Scene/Scene.hpp>
#include <Scene/MeshRenderer.hpp>

using namespace std;

Environment::Environment(Scene* scene) : 
	mTimeOfDay(.25f),
	mScene(scene), mSkybox(nullptr),
	mIncomingLight(float4(2.5f)),
	mRayleighScatterCoef(2),
	mRayleighExtinctionCoef(.5f),
	mMieScatterCoef(1),
	mMieExtinctionCoef(1),
	mMieG(0.76f),
	mDistanceScale(200),
	mSunIntensity(.1f),
	mAtmosphereHeight(80000.0f),
	mPlanetRadius(6371000.0f),
	mDensityScale(float4(20000, 8000, 0, 0)),
	mRayleighSct(float4(5.8f, 13.5f, 33.1f, 0) * .000001f),
	mMieSct(float4(2, 2, 2, 0) * .000001f),
	mAmbientLight(0),
	mMoonSize(.04f) {

	shared_ptr<Material> skyboxMat = make_shared<Material>("Skybox", mScene->AssetManager()->LoadShader("Shaders/skybox.shader"));
	shared_ptr<MeshRenderer> skybox = make_shared<MeshRenderer>("SkyCube");
	skybox->LocalScale(1e23f);
	skybox->Mesh(shared_ptr<Mesh>(Mesh::CreateCube("Cube", mScene->Instance())));
	skybox->Material(skyboxMat);
	mSkybox = skybox.get();
	mScene->AddObject(skybox);

	mSkyboxMaterial = skyboxMat.get();

	mShader = mScene->AssetManager()->LoadShader("Shaders/scatter.shader");
	mMoonTexture = mScene->AssetManager()->LoadTexture("Assets/moon.png");
	mStarTexture = mScene->AssetManager()->LoadCubemap("Assets/stars/posx.png", "Assets/stars/negx.png", "Assets/stars/posy.png", "Assets/stars/negy.png", "Assets/stars/posz.png", "Assets/stars/negz.png", false);

	float4 scatterR = mRayleighSct * mRayleighScatterCoef;
	float4 scatterM = mMieSct * mMieScatterCoef;
	float4 extinctR = mRayleighSct * mRayleighExtinctionCoef;
	float4 extinctM = mMieSct * mMieExtinctionCoef;

	uint8_t r[256 * 4];
	for (uint32_t i = 0; i < 256; i++) {
		r[4 * i + 0] = rand() % 0xFF;
		r[4 * i + 1] = rand() % 0xFF;
		r[4 * i + 2] = rand() % 0xFF;
		r[4 * i + 3] = 0;
	}
	Texture* randTex = new Texture("Random Vectors", mScene->Instance(), r, 256 * 4, 16, 16, 1, VK_FORMAT_R8G8B8A8_SNORM, 1,
		VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);

	for (uint32_t i = 0; i < mScene->Instance()->DeviceCount(); i++) {
		Device* device = mScene->Instance()->GetDevice(i);
		auto commandBuffer = device->GetCommandBuffer();

		DevLUT dlut = {};

		dlut.mParticleDensityLUT = new Texture("Particle Density LUT", device, 1024, 1024, 1, VK_FORMAT_R32G32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
		dlut.mSkyboxLUT = new Texture("Skybox LUT", commandBuffer->Device(), 32, 128, 32, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		dlut.mSkyboxLUT2 = new Texture("Skybox LUT 2", commandBuffer->Device(), 32, 128, 32, VK_FORMAT_R32G32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

		// compute particle density LUT
		dlut.mParticleDensityLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer.get());
		dlut.mSkyboxLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer.get());
		dlut.mSkyboxLUT2->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL, commandBuffer.get());

		ComputeShader* particleDensity = mShader->GetCompute(commandBuffer->Device(), "ParticleDensityLUT", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, particleDensity->mPipeline);

		DescriptorSet* ds = new DescriptorSet("Particle Density", device, particleDensity->mDescriptorSetLayouts[0]);
		ds->CreateStorageTextureDescriptor(dlut.mParticleDensityLUT, particleDensity->mDescriptorBindings.at("_RWParticleDensityLUT").second.binding);
		VkDescriptorSet vds = *ds;
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, particleDensity->mPipelineLayout, 0, 1, &vds, 0, nullptr);
		commandBuffer->PushConstant(particleDensity, "_AtmosphereHeight", &mAtmosphereHeight);
		commandBuffer->PushConstant(particleDensity, "_PlanetRadius", &mPlanetRadius);
		commandBuffer->PushConstant(particleDensity, "_DensityScaleHeight", &mDensityScale);
		vkCmdDispatch(*commandBuffer, 1024, 1024, 1);

		dlut.mParticleDensityLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer.get());

		// compute skybox LUT
		ComputeShader* skybox = mShader->GetCompute(commandBuffer->Device(), "SkyboxLUT", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, skybox->mPipeline);

		DescriptorSet* ds2 = new DescriptorSet("Scatter LUT", device, skybox->mDescriptorSetLayouts[0]);
		ds2->CreateStorageTextureDescriptor(dlut.mSkyboxLUT, skybox->mDescriptorBindings.at("_SkyboxLUT").second.binding);
		ds2->CreateStorageTextureDescriptor(dlut.mSkyboxLUT2, skybox->mDescriptorBindings.at("_SkyboxLUT2").second.binding);
		ds2->CreateSampledTextureDescriptor(dlut.mParticleDensityLUT, skybox->mDescriptorBindings.at("_ParticleDensityLUT").second.binding);
		vds = *ds2;
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, skybox->mPipelineLayout, 0, 1, &vds, 0, nullptr);

		commandBuffer->PushConstant(skybox, "_AtmosphereHeight", &mAtmosphereHeight);
		commandBuffer->PushConstant(skybox, "_PlanetRadius", &mPlanetRadius);
		commandBuffer->PushConstant(skybox, "_DensityScaleHeight", &mDensityScale);
		commandBuffer->PushConstant(skybox, "_ScatteringR", &scatterR);
		commandBuffer->PushConstant(skybox, "_ScatteringM", &scatterM);
		commandBuffer->PushConstant(skybox, "_ExtinctionR", &extinctR);
		commandBuffer->PushConstant(skybox, "_ExtinctionM", &extinctM);
		commandBuffer->PushConstant(skybox, "_IncomingLight", &mIncomingLight);
		commandBuffer->PushConstant(skybox, "_MieG", &mMieG);
		commandBuffer->PushConstant(skybox, "_SunIntensity", &mSunIntensity);
		vkCmdDispatch(*commandBuffer, 32, 128, 32);

		mDeviceLUTs.emplace(device, dlut);

		dlut.mSkyboxLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer.get());
		dlut.mSkyboxLUT2->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer.get());

		device->Execute(commandBuffer, false)->Wait();

		delete ds;
		delete ds2;
	}

	{
		Device* device = mScene->Instance()->GetDevice(0);
		auto commandBuffer = device->GetCommandBuffer();

		Buffer ambientBuffer("Ambient", device, 128 * sizeof(float4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		Buffer dirBuffer("Direct", device, 128 * sizeof(float4), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		ComputeShader* ambient = mShader->GetCompute(commandBuffer->Device(), "AmbientLightLUT", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ambient->mPipeline);

		DescriptorSet* ds = new DescriptorSet("Ambient LUT", device, ambient->mDescriptorSetLayouts[0]);
		ds->CreateStorageBufferDescriptor(&ambientBuffer, ambient->mDescriptorBindings.at("_RWAmbientLightLUT").second.binding);
		ds->CreateStorageTextureDescriptor(randTex, ambient->mDescriptorBindings.at("_RandomVectors").second.binding);
		ds->CreateSampledTextureDescriptor(mDeviceLUTs.at(device).mParticleDensityLUT, ambient->mDescriptorBindings.at("_ParticleDensityLUT").second.binding);
		VkDescriptorSet vds = *ds;
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ambient->mPipelineLayout, 0, 1, &vds, 0, nullptr);

		commandBuffer->PushConstant(ambient, "_AtmosphereHeight", &mAtmosphereHeight);
		commandBuffer->PushConstant(ambient, "_PlanetRadius", &mPlanetRadius);
		commandBuffer->PushConstant(ambient, "_DensityScaleHeight", &mDensityScale);
		commandBuffer->PushConstant(ambient, "_ScatteringR", &scatterR);
		commandBuffer->PushConstant(ambient, "_ScatteringM", &scatterM);
		commandBuffer->PushConstant(ambient, "_ExtinctionR", &extinctR);
		commandBuffer->PushConstant(ambient, "_ExtinctionM", &extinctM);
		commandBuffer->PushConstant(ambient, "_IncomingLight", &mIncomingLight);
		commandBuffer->PushConstant(ambient, "_MieG", &mMieG);
		commandBuffer->PushConstant(ambient, "_SunIntensity", &mSunIntensity);
		vkCmdDispatch(*commandBuffer, 128, 1, 1);


		ComputeShader* direct = mShader->GetCompute(commandBuffer->Device(), "DirectLightLUT", {});
		vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, direct->mPipeline);

		DescriptorSet* ds2 = new DescriptorSet("Ambient LUT", device, direct->mDescriptorSetLayouts[0]);
		ds2->CreateStorageBufferDescriptor(&dirBuffer, direct->mDescriptorBindings.at("_RWDirectLightLUT").second.binding);
		ds2->CreateSampledTextureDescriptor(mDeviceLUTs.at(device).mParticleDensityLUT, direct->mDescriptorBindings.at("_ParticleDensityLUT").second.binding);
		vds = *ds2;
		vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, direct->mPipelineLayout, 0, 1, &vds, 0, nullptr);

		commandBuffer->PushConstant(direct, "_AtmosphereHeight", &mAtmosphereHeight);
		commandBuffer->PushConstant(direct, "_PlanetRadius", &mPlanetRadius);
		commandBuffer->PushConstant(direct, "_DensityScaleHeight", &mDensityScale);
		commandBuffer->PushConstant(direct, "_ScatteringR", &scatterR);
		commandBuffer->PushConstant(direct, "_ScatteringM", &scatterM);
		commandBuffer->PushConstant(direct, "_ExtinctionR", &extinctR);
		commandBuffer->PushConstant(direct, "_ExtinctionM", &extinctM);
		commandBuffer->PushConstant(direct, "_IncomingLight", &mIncomingLight);
		commandBuffer->PushConstant(direct, "_MieG", &mMieG);
		commandBuffer->PushConstant(direct, "_SunIntensity", &mSunIntensity);
		vkCmdDispatch(*commandBuffer, 128, 1, 1);

		device->Execute(commandBuffer, false)->Wait();

		delete ds;
		delete ds2;

		ambientBuffer.Map();
		dirBuffer.Map();
		std::memcpy(mAmbientLUT, ambientBuffer.MappedData(), 128 * sizeof(float4));
		std::memcpy(mDirectionalLUT, dirBuffer.MappedData(), 128 * sizeof(float4));
	}

	delete randTex;

	shared_ptr<Light> sun = make_shared<Light>("Sun");
	mScene->AddObject(sun);
	sun->CastShadows(true);
	sun->ShadowDistance(1024);
	sun->Color(float3(1, .99f, .95f));
	sun->LocalRotation(quaternion(float3(PI / 4, PI / 4, 0)));
	sun->Type(Sun);
	mSun = sun.get();

	shared_ptr<Light> moon = make_shared<Light>("Moon");
	mScene->AddObject(moon);
	moon->mEnabled = false;
	moon->CastShadows(true);
	moon->ShadowDistance(1024);
	moon->Color(float3(1));
	moon->Intensity(.05f);
	moon->LocalRotation(quaternion(float3(PI / 4, PI / 4, 0)));
	moon->Type(Sun);
	mMoon = moon.get();
}
Environment::~Environment() {
	mScene->RemoveObject(mSun);
	mScene->RemoveObject(mMoon);

	for (auto t : mCameraLUTs) {
		for (uint32_t i = 0; i < t.first->Device()->MaxFramesInFlight(); i++) {
			safe_delete(t.second[i].mInscatterLUT);
			safe_delete(t.second[i].mOutscatterLUT);
		}
		safe_delete_array(t.second);
	}
	for (auto t : mDeviceLUTs) {
		safe_delete(t.second.mParticleDensityLUT);
		safe_delete(t.second.mSkyboxLUT);
		safe_delete(t.second.mSkyboxLUT2);
	}
	mScene->RemoveObject(mSkybox);
}

void Environment::SetEnvironment(Camera* camera, Material* mat) {
	CamLUT* l = mCameraLUTs.at(camera) + camera->Device()->FrameContextIndex();
	mat->SetParameter("InscatteringLUT", l->mInscatterLUT);
	mat->SetParameter("ExtinctionLUT", l->mOutscatterLUT);
	mat->SetParameter("AmbientLight", mAmbientLight);
}

void Environment::Update() {
	const float sunsetDuration = .04f;
	const float moonIntensity = .2f;

	if (mTimeOfDay > .5f - sunsetDuration && mTimeOfDay < .5f + sunsetDuration) {
		// sunset
		mSun->mEnabled = true;
		mMoon->mEnabled = true;
		float f = (mTimeOfDay - (.5f - sunsetDuration)) / (2 * sunsetDuration);
		mMoon->Intensity(moonIntensity * f);
	} else if (mTimeOfDay < sunsetDuration || mTimeOfDay > 1 - sunsetDuration) {
		// sunrise
		mSun->mEnabled = true;
		mMoon->mEnabled = true;
		float f = 0;
		if (mTimeOfDay < sunsetDuration)
			f = .5f + .5f * mTimeOfDay / sunsetDuration;
		else
			f = .5f * (mTimeOfDay - (1 - sunsetDuration)) / sunsetDuration;
		mMoon->Intensity(moonIntensity * (1 - f));
	} else if (mTimeOfDay > .5f + sunsetDuration) {
		// night time
		mSun->mEnabled = false;
		mMoon->mEnabled = true;
		mMoon->Intensity(moonIntensity);
	} else if (mTimeOfDay > sunsetDuration) {
		// day time
		mSun->mEnabled = true;
		mMoon->mEnabled = false;
	}

	float fwd = -10 * mMoon->WorldRotation().forward().y + .1f;
	mMoon->Intensity(mMoon->Intensity() * clamp(fwd * fwd * fwd, 0.f, 1.f));

	mSun->LocalRotation(quaternion(float3(0, radians(23.5f), radians(23.5f))) * quaternion(float3(mTimeOfDay * PI * 2, 0, 0)));
	mMoon->LocalRotation(quaternion(float3(0, radians(70.f), radians(70.f))) * quaternion(float3(PI + mTimeOfDay * PI * 2, 0, 0)));

	float cosAngle = dot(float3(0, 1, 0), -mSun->WorldRotation().forward());
	float u = (cosAngle + 0.1f) / 1.1f;// * 0.5f + 0.5f;
	u = u * 128;
	int index0 = (int)u;
	int index1 = index0 + 1;
	float weight1 = u - index0;
	float weight0 = 1 - weight1;
	index0 = clamp(index0, 0, 128 - 1);
	index1 = clamp(index1, 0, 128 - 1);

	mSun->Color((1.055f * pow((mDirectionalLUT[index0] * weight0 + mDirectionalLUT[index1] * weight1).rgb, 1.f / 2.4f) - .055f));
	mAmbientLight = .1f * length(1.055f * pow(mAmbientLUT[index0] * weight0 + mAmbientLUT[index1] * weight1, 1.f / 2.4f) - .055f);
}

void Environment::PreRender(CommandBuffer* commandBuffer, Camera* camera) {
	if (mCameraLUTs.count(camera) == 0) {
		CamLUT* t = new CamLUT[commandBuffer->Device()->MaxFramesInFlight()];
		memset(t, 0, sizeof(CamLUT) * commandBuffer->Device()->MaxFramesInFlight());
		mCameraLUTs.emplace(camera, t);
	}
	CamLUT* l = mCameraLUTs.at(camera) + commandBuffer->Device()->FrameContextIndex();
	DevLUT* dlut = &mDeviceLUTs.at(camera->Device());

	if (!l->mInscatterLUT) {
		l->mInscatterLUT = new Texture("Inscatter LUT", commandBuffer->Device(), 16, 16, 128, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		l->mInscatterLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);
	}
	if (!l->mOutscatterLUT) {
		l->mOutscatterLUT = new Texture("Outscatter LUT", commandBuffer->Device(), 16, 16, 128, VK_FORMAT_R32G32B32A32_SFLOAT, VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		l->mOutscatterLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);
	}

	l->mInscatterLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);
	l->mOutscatterLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, commandBuffer);

	float3 r0 = camera->ScreenToWorldRay(float2(0, 1)).mDirection * camera->Far();
	float3 r1 = camera->ScreenToWorldRay(float2(0, 0)).mDirection * camera->Far();
	float3 r2 = camera->ScreenToWorldRay(float2(1, 0)).mDirection * camera->Far();
	float3 r3 = camera->ScreenToWorldRay(float2(1, 1)).mDirection * camera->Far();
	float4 scatterR = mRayleighSct * mRayleighScatterCoef;
	float4 scatterM = mMieSct * mMieScatterCoef;
	float4 extinctR = mRayleighSct * mRayleighExtinctionCoef;
	float4 extinctM = mMieSct * mMieExtinctionCoef;
	float3 cp = camera->WorldPosition();
	float3 lightdir = -normalize(mSun->WorldRotation().forward());

	#pragma region Precompute scattering
	ComputeShader* scatter = mShader->GetCompute(commandBuffer->Device(), "InscatteringLUT", {});

	vkCmdBindPipeline(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, scatter->mPipeline);
	DescriptorSet* ds = commandBuffer->Device()->GetTempDescriptorSet("Scatter LUT", scatter->mDescriptorSetLayouts[0]);
	ds->CreateStorageTextureDescriptor(l->mInscatterLUT, scatter->mDescriptorBindings.at("_InscatteringLUT").second.binding);
	ds->CreateStorageTextureDescriptor(l->mOutscatterLUT, scatter->mDescriptorBindings.at("_ExtinctionLUT").second.binding);
	ds->CreateSampledTextureDescriptor(dlut->mParticleDensityLUT, scatter->mDescriptorBindings.at("_ParticleDensityLUT").second.binding);

	VkDescriptorSet vds = *ds;
	vkCmdBindDescriptorSets(*commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, scatter->mPipelineLayout, 0, 1, &vds, 0, nullptr);

	commandBuffer->PushConstant(scatter, "_BottomLeftCorner", &r0);
	commandBuffer->PushConstant(scatter, "_TopLeftCorner", &r1);
	commandBuffer->PushConstant(scatter, "_TopRightCorner", &r2);
	commandBuffer->PushConstant(scatter, "_BottomRightCorner", &r3);

	commandBuffer->PushConstant(scatter, "_AtmosphereHeight", &mAtmosphereHeight);
	commandBuffer->PushConstant(scatter, "_PlanetRadius", &mPlanetRadius);
	commandBuffer->PushConstant(scatter, "_LightDir", &lightdir);
	commandBuffer->PushConstant(scatter, "_CameraPos", &cp);
	commandBuffer->PushConstant(scatter, "_DensityScaleHeight", &mDensityScale);
	commandBuffer->PushConstant(scatter, "_ScatteringR", &scatterR);
	commandBuffer->PushConstant(scatter, "_ScatteringM", &scatterM);
	commandBuffer->PushConstant(scatter, "_ExtinctionR", &extinctR);
	commandBuffer->PushConstant(scatter, "_ExtinctionM", &extinctM);
	commandBuffer->PushConstant(scatter, "_IncomingLight", &mIncomingLight);
	commandBuffer->PushConstant(scatter, "_MieG", &mMieG);
	commandBuffer->PushConstant(scatter, "_DistanceScale", &mDistanceScale);
	commandBuffer->PushConstant(scatter, "_SunIntensity", &mSunIntensity);
	vkCmdDispatch(*commandBuffer, 16, 16, 1);
	#pragma endregion

	l->mInscatterLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);
	l->mOutscatterLUT->TransitionImageLayout(VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, commandBuffer);

	mSkyboxMaterial->SetParameter("_SkyboxLUT", dlut->mSkyboxLUT);
	mSkyboxMaterial->SetParameter("_SkyboxLUT2", dlut->mSkyboxLUT2);
	mSkyboxMaterial->SetParameter("_MoonTex", mMoonTexture);
	mSkyboxMaterial->SetParameter("_StarCube", mStarTexture);
	mSkyboxMaterial->SetParameter("_MoonDir", -mMoon->WorldRotation().forward());
	mSkyboxMaterial->SetParameter("_MoonRight", mMoon->WorldRotation() * float3(-1,0,0));
	mSkyboxMaterial->SetParameter("_MoonSize", mMoonSize);
	mSkyboxMaterial->SetParameter("_IncomingLight", mIncomingLight.xyz);
	mSkyboxMaterial->SetParameter("_SunDir", lightdir);
	mSkyboxMaterial->SetParameter("_PlanetRadius", mPlanetRadius);
	mSkyboxMaterial->SetParameter("_AtmosphereHeight", mAtmosphereHeight);
	mSkyboxMaterial->SetParameter("_SunIntensity", mSunIntensity);
	mSkyboxMaterial->SetParameter("_MieG", mMieG);
	mSkyboxMaterial->SetParameter("_ScatteringR", scatterR.xyz);
	mSkyboxMaterial->SetParameter("_ScatteringM", scatterM.xyz);
	mSkyboxMaterial->SetParameter("_StarRotation", quaternion(float3(-mTimeOfDay * PI * 2, 0, 0)).xyzw);
	mSkyboxMaterial->SetParameter("_StarFade", clamp(lightdir.y*10.f, 0.f, 1.f));
}