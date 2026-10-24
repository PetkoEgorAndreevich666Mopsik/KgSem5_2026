#pragma once

#include <string>

struct Material {
    std::string Name;
    std::string DiffuseTextureName;
    std::string NormalTextureName;
    std::string DisplacementTextureName;
    float Shininess = 32.0f;
};
