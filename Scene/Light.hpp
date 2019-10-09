#pragma once

#include <Scene/Object.hpp>
#include <Util/Util.hpp>

#include <Shaders/shadercompat.h>

enum LightType {
	Sun = LIGHT_SUN,
	Point = LIGHT_POINT,
	Spot = LIGHT_SPOT,
};

class Light : public Object {
public:
	ENGINE_EXPORT Light(const std::string& name);
	ENGINE_EXPORT ~Light();

	inline void Color(const float3& c) { mColor = c; }
	inline float3 Color() const { return mColor; }

	inline void Intensity(float i) { mIntensity = i; }
	inline float Intensity() const { return mIntensity; }

	inline void Type(LightType t) { mType = t; }
	inline LightType Type() const { return mType; }

	inline void SpotAngle(float a) { mSpotAngle = a; }
	inline float SpotAngle() const { return mSpotAngle; }

	// Distance the light can travel
	inline void Range(float r) { mRange = r; }
	// Distance the light can travel
	inline float Range() const { return mRange; }

	// Physical radius of the point/spot light
	inline void Radius(float r) { mRadius = r; }
	// Physical radius of the point/spot light
	inline float Radius() const { return mRadius; }

	inline AABB Bounds() override {
		switch (mType) {
		case Point:
			return AABB(WorldPosition(), float3(mRange));
		case Spot:
			return AABB(float3(0, 0, mRange*.5f), float3(mRange * sin(float2(mSpotAngle * .5f)), mRange * .5f)) * ObjectToWorld();
		case Sun:
			return AABB(float3(), float3(1e20f));
		}
		throw std::runtime_error("Invalid light type!");
	}

private:
	float3 mColor;
	float mIntensity;
	LightType mType;
	// Physical radius of the point/spot light
	float mRadius;
	// Distance the light can travel
	float mRange;
	float mSpotAngle;
};