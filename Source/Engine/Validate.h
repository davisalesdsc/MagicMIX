// Validador determinístico: nada do LLM chega ao áudio sem passar por aqui.
#pragma once

#include <string>
#include <vector>

#include "ChainSpec.h"

namespace mm
{
// Valor do parâmetro para a posição do macro (linear, ou log quando `log`).
float interpolate (float valueAt0, float valueAt1, float position, bool log);

// Cópia segura do spec. Cada correção feita entra em `report`.
// Com `features`, aplica também os limites que dependem do sinal.
ChainSpec validate (const ChainSpec& spec, const Features* features, std::vector<std::string>& report);
}
