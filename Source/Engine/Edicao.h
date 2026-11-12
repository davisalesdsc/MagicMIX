// Edição manual de uma cadeia pela interface: atualiza o spec e devolve a edição para a cadeia que está tocando.
#pragma once

#include <string>

#include "Chain.h"
#include "ChainSpec.h"

namespace mm
{
// Ajusta um parâmetro à mão. O valor é limitado à faixa do catálogo. Se um macro controla esse parâmetro,
// os dois extremos do macro andam juntos para o valor novo ficar exatamente na posição atual do botão:
// o ajuste do músico vale, e o macro continua crescendo ou diminuindo a partir dele.
// `macroPositions` tem kMaxMacros posições. `ok` fica false se o módulo ou o parâmetro não existir.
Chain::Edit editarParametro (ChainSpec& spec, int moduleIndex, const std::string& param, float value,
                             const float* macroPositions, bool& ok);

// Liga ou desliga um módulo inteiro.
Chain::Edit ligarModulo (ChainSpec& spec, int moduleIndex, bool ligado);

// Valor que o parâmetro tem agora, com os macros na posição dada (o que a interface deve mostrar).
float valorAtual (const ChainSpec& spec, int moduleIndex, const std::string& param, const float* macroPositions);
}
