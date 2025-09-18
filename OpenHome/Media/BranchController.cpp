#include <OpenHome/Media/BranchController.h>
#include <OpenHome/Media/Pipeline/Brancher.h>

#include <algorithm>

using namespace OpenHome;
using namespace Media;


// BranchController

BranchController::BranchController()
{
}

IBrancher& BranchController::GetBrancher(const Brx& aId)
{
    auto it = GetIterator(aId);
    return *(*it);
}

void BranchController::SetEnabled(const Brx& aId, TBool aEnable)
{
    auto it = GetIterator(aId);
    const auto priority = (*it)->Priority();
    if (priority == IBrancher::EPriority::Exclusive && aEnable) {
        DisableAll();
    }
    (*it)->SetEnabled(aEnable);
}

void BranchController::AttachBrancher(IBrancherControllable& aBrancher)
{
    iBranchers.push_back(&aBrancher);
}

void BranchController::RemoveBrancher(const Brx& aId)
{
    auto it = GetIterator(aId);
    iBranchers.erase(it);
}

void BranchController::DisableAll()
{
    for (auto brancher : iBranchers) {
        if (brancher->Priority() == IBrancher::EPriority::AlwaysOn) {
            continue;
        }
        brancher->SetEnabled(false);
    }
}

std::vector<IBrancherControllable*>::iterator BranchController::GetIterator(const Brx& aId)
{
    auto it = std::find_if(iBranchers.begin(), iBranchers.end(), [&](IBrancher* aBrancher) {
        return (aBrancher->Id() == aId);
    });

    if (it == iBranchers.end()) {
        THROW(BranchControllerError);
    }

    return it;
}

