#include "Sessao.h"

#include <algorithm>

namespace mm
{
void Sessao::entrar (MagicMixProcessor* p)
{
    {
        const juce::ScopedLock sl (lock);
        if (std::find (lista.begin(), lista.end(), p) == lista.end())
            lista.push_back (p);
    }
    sendChangeMessage();
}

void Sessao::sair (MagicMixProcessor* p)
{
    std::vector<Ouvinte*> avisar;
    {
        const juce::ScopedLock sl (lock);
        lista.erase (std::remove (lista.begin(), lista.end(), p), lista.end());
        avisar = ouvintes;
    }
    for (auto* o : avisar)
        o->instanciaSaindo (p);
    sendChangeMessage();
}

std::vector<MagicMixProcessor*> Sessao::instancias() const
{
    const juce::ScopedLock sl (lock);
    return lista;
}

void Sessao::adicionarOuvinte (Ouvinte* o)
{
    const juce::ScopedLock sl (lock);
    ouvintes.push_back (o);
}

void Sessao::removerOuvinte (Ouvinte* o)
{
    const juce::ScopedLock sl (lock);
    ouvintes.erase (std::remove (ouvintes.begin(), ouvintes.end(), o), ouvintes.end());
}
}
