#include <OpenHome/Media/BranchController.h>
#include <OpenHome/Media/Pipeline/Brancher.h>

#include <algorithm>

using namespace OpenHome;
using namespace Media;


// BranchController

BranchController::BranchController()
    : iDefaultSet(false)
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
    if (priority == IBrancher::EPriority::Default ||
        priority == IBrancher::EPriority::Exclusive) {

        aEnable ? DisableAll() : EnableDefault();
    }
    (*it)->SetEnabled(aEnable);
}

void BranchController::AttachBrancher(IBrancherControllable& aBrancher)
{
    if (aBrancher.Priority() == IBrancher::EPriority::Default) {
        if (iDefaultSet) {
            THROW(BranchControllerError);
        }
        iDefaultSet = true;
    }
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
        brancher->SetEnabled(false);
    }
}

void BranchController::EnableDefault()
{
    if (!iDefaultSet) {
        return;
    }

    for (auto brancher : iBranchers) {
        if (brancher->Priority() == IBrancher::EPriority::Default) {
            brancher->SetEnabled(true);
        }
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

