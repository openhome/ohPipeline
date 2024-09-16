#pragma once

#include <OpenHome/Media/Pipeline/Brancher.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>

#include <vector>

EXCEPTION(BranchControllerError);

namespace OpenHome {
namespace Media {

class IBranchController
{
public:
    virtual IBrancher& GetBrancher(const Brx& aId) = 0;
    virtual void SetEnabled(const Brx& aId, TBool aEnable) = 0;

    virtual ~IBranchController() {}
};

class BranchController : public IBranchController
{
public:
    BranchController();
public: // from IBranchController
    IBrancher& GetBrancher(const Brx& aId) override;
    void SetEnabled(const Brx& aId, TBool aEnable) override;
public:
    void AttachBrancher(IBrancherControllable& aBrancher);
    void RemoveBrancher(const Brx& aId);
private:
    void DisableAll();
    void EnableDefault();
    std::vector<IBrancherControllable*>::iterator GetIterator(const Brx& aId);
private:
    TBool iDefaultSet;
    std::vector<IBrancherControllable*> iBranchers;
};

}
}