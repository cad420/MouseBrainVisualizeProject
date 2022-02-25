//
// Created by wyz on 2022/2/25.
//
#pragma once
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/hash.hpp>

#include "Define.hpp"

MRAYNS_BEGIN

using Vector2i = glm::ivec2;
using Vector3i = glm::ivec3;
using Vector4i = glm::ivec4;

using Vector2ui = glm::uvec2;
using Vector3ui = glm::uvec3;
using Vector4ui = glm::uvec4;

using Vector2f = glm::vec2;
using Vector3f = glm::vec3;
using Vector4f = glm::vec4;

using Vector2d = glm::dvec2;
using Vector3d = glm::dvec3;
using Vector4d = glm::dvec4;

using Matrix3f = glm::mat3;
using Matrix3d = glm::dmat3;
using Matrix4f = glm::mat4;
using Matrix4d = glm::dmat4;

using RGBA = glm::vec<4,uint8_t>;
using RGB = glm::vec<3,uint8_t>;

MRAYNS_END
