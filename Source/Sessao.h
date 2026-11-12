// Sessão: todas as instâncias do MagicMIX abertas no mesmo projeto da DAW.
//
// As DAWs carregam os plugins no mesmo processo, então as instâncias se enxergam por aqui. É o que
// permite a coluna de pistas: de qualquer janela do MagicMIX dá para ver e abrir as outras pistas.
#pragma once

#include <vector>

#include <juce_events/juce_events.h>

namespace mm
{
class MagicMixProcessor;

class Sessao : public juce::ChangeBroadcaster
{
public:
    // Quem guarda ponteiros para instâncias precisa saber, na hora, quando uma delas some.
    struct Ouvinte
    {
        virtual ~Ouvinte() = default;
        virtual void instanciaSaindo (MagicMixProcessor*) = 0;
    };

    void entrar (MagicMixProcessor* p);
    void sair (MagicMixProcessor* p);
    std::vector<MagicMixProcessor*> instancias() const;
    void avisar() { sendChangeMessage(); }  // nome, nível ou cadeia de alguma pista mudou

    void adicionarOuvinte (Ouvinte* o);
    void removerOuvinte (Ouvinte* o);

private:
    mutable juce::CriticalSection lock;
    std::vector<MagicMixProcessor*> lista;
    std::vector<Ouvinte*> ouvintes;
};
}
