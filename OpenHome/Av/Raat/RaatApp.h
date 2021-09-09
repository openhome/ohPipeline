#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>

namespace OpenHome {
namespace Av {

class RattApp
{
public:
    RattApp();
    ~RattApp();
private:
    void RaatThread();
private:
    ThreadFunctor iThread;
};

}
}