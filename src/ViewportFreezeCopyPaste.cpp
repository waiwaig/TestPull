//-----------------------------------------------------------------------------
// ViewportFreezeCopyPaste.cpp
// ObjectARX 命令：框选多视口+实体，跨文档复制/粘贴图层冻结状态
//
// 命令:
//   VpFreezeCopy   — 框选源实体（视口+多段线等），复制到剪贴板并携带冻结状态
//   VpFreezePaste  — 在目标文档粘贴，自动恢复各视口的冻结状态
//-----------------------------------------------------------------------------

#include <aced.h>
#include <dbapserv.h>
#include <dbviewp.h>
#include <dblayer.h>
#include <adslib.h>
#include <map>
#include <vector>
#include <algorithm>

//-----------------------------------------------------------------------------
// 一个视口的图层冻结状态集
//-----------------------------------------------------------------------------
struct ViewportFreezeState
{
    std::map<CString, bool> layerStates; // layerName -> true=冻结, false=解冻
};

// 全局存储：所有被选中视口的冻结状态（跨文档保留，索引与 XData 中的 index 对应）
static std::vector<ViewportFreezeState> g_copiedViewportStates;

// XData 标记常量
static const TCHAR* kAppName = _T("VPFREECOPY");
static const int kIdxGroupCode = 1070; // DxfInt16

//-----------------------------------------------------------------------------
// 判断对象是否为 AcDbViewport (布局视口)
//-----------------------------------------------------------------------------
static bool IsLayoutViewport(AcDbObjectId objId)
{
    AcDbViewport *pVp = nullptr;
    if (acdbOpenAcDbObject(acdb_cast2(AcDbObject*, &pVp), objId,
                           AcDb::kForRead) != Acad::eOk)
        return false;

    bool isVp = pVp->isKindOf(AcDbViewport::desc());
    pVp->close();
    return isVp;
}

//-----------------------------------------------------------------------------
// 为实体附加 XData： (1001 . "VPFREECOPY") (1070 . index)
//-----------------------------------------------------------------------------
static bool AttachIndexXData(AcDbObjectId objId, int index)
{
    AcDbEntity *pEnt = nullptr;
    if (acdbOpenAcDbObject(acdb_cast2(AcDbObject*, &pEnt), objId,
                           AcDb::kForWrite) != Acad::eOk)
        return false;

    struct resbuf *pRb = acutBuildList(
        AcDb::kDxfRegAppName, kAppName,
        kIdxGroupCode, (short)index,
        0);

    Acad::ErrorStatus es = pEnt->setXData(pRb);
    acutRelRb(pRb);
    pEnt->close();
    return es == Acad::eOk;
}

//-----------------------------------------------------------------------------
// 从实体的 XData 中读取 index
//-----------------------------------------------------------------------------
static bool ReadIndexXData(AcDbObjectId objId, int &outIndex)
{
    AcDbEntity *pEnt = nullptr;
    if (acdbOpenAcDbObject(acdb_cast2(AcDbObject*, &pEnt), objId,
                           AcDb::kForRead) != Acad::eOk)
        return false;

    struct resbuf *pRb = pEnt->xData(kAppName);
    bool found = false;

    if (pRb != nullptr) {
        struct resbuf *pVal = pRb->rbnext; // skip app name
        if (pVal != nullptr && pVal->restype == kIdxGroupCode) {
            outIndex = pVal->resval.rInt16;
            found = true;
        }
        acutRelRb(pRb);
    }

    pEnt->close();
    return found;
}

//-----------------------------------------------------------------------------
// 移除实体的 VPFREECOPY XData
//-----------------------------------------------------------------------------
static bool RemoveOurXData(AcDbObjectId objId)
{
    AcDbEntity *pEnt = nullptr;
    if (acdbOpenAcDbObject(acdb_cast2(AcDbObject*, &pEnt), objId,
                           AcDb::kForWrite) != Acad::eOk)
        return false;

    // 仅含 app name 的 resbuf → 移除该 app 的所有 XData
    struct resbuf *pRb = acutBuildList(AcDb::kDxfRegAppName, kAppName, 0);
    Acad::ErrorStatus es = pEnt->setXData(pRb);
    acutRelRb(pRb);
    pEnt->close();
    return es == Acad::eOk;
}

//-----------------------------------------------------------------------------
// 收集一个视口的图层冻结状态
//-----------------------------------------------------------------------------
static ViewportFreezeState CollectViewportFreezeState(AcDbObjectId vpId)
{
    ViewportFreezeState state;

    AcDbLayerTable *pLayerTbl = nullptr;
    if (acdbCurDwg()->getLayerTable(pLayerTbl, AcDb::kForRead) != Acad::eOk)
        return state;

    AcDbLayerTableIterator *pIter = nullptr;
    pLayerTbl->newIterator(pIter);

    for (; !pIter->done(); pIter->step()) {
        AcDbObjectId layerId;
        pIter->getRecordId(layerId);

        AcDbLayerTableRecord *pLyrRec = nullptr;
        if (acdbOpenAcDbObject(acdb_cast2(AcDbObject*, &pLyrRec),
                               layerId, AcDb::kForRead) == Acad::eOk) {
            CString name = pLyrRec->getName();
            bool frozen = pLyrRec->isFrozenInViewport(vpId);
            state.layerStates[name] = frozen;
            pLyrRec->close();
        }
    }

    delete pIter;
    pLayerTbl->close();
    return state;
}

//-----------------------------------------------------------------------------
// 将冻结状态应用到目标视口
//-----------------------------------------------------------------------------
static struct ApplyResult
{
    int changed;
    int locked;
    int skipped;
} ApplyViewportFreezeState(AcDbObjectId tgtVpId, const ViewportFreezeState &state)
{
    ApplyResult r = {0, 0, 0};

    AcDbLayerTable *pLayerTbl = nullptr;
    if (acdbCurDwg()->getLayerTable(pLayerTbl, AcDb::kForRead) != Acad::eOk)
        return r;

    AcDbLayerTableIterator *pIter = nullptr;
    pLayerTbl->newIterator(pIter);

    for (; !pIter->done(); pIter->step()) {
        AcDbObjectId layerId;
        pIter->getRecordId(layerId);

        AcDbLayerTableRecord *pLyrRec = nullptr;
        if (acdbOpenAcDbObject(acdb_cast2(AcDbObject*, &pLyrRec),
                               layerId, AcDb::kForWrite) == Acad::eOk) {
            CString name = pLyrRec->getName();
            auto it = state.layerStates.find(name);

            if (it != state.layerStates.end()) {
                if (pLyrRec->isLocked()) {
                    r.locked++;
                    pLyrRec->close();
                    continue;
                }

                bool current = pLyrRec->isFrozenInViewport(tgtVpId);
                if (current != it->second) {
                    pLyrRec->setIsFrozenInViewport(tgtVpId, it->second);
                    r.changed++;
                }
            } else {
                r.skipped++;
            }
            pLyrRec->close();
        }
    }

    delete pIter;
    pLayerTbl->close();
    return r;
}

//-----------------------------------------------------------------------------
// 命令：CopyVpFreeze
//   — 框选源实体，复制到剪贴板并携带视口的冻结状态
//-----------------------------------------------------------------------------
static void ACRX_CMD_ENTRYPOINT copyVpFreeze()
{
    // --- 用户框选实体 ---
    acutPrintf(_T("\n框选要复制的实体（视口、多段线等）: "));
    ads_name sset;
    int ret = acedSSGet(NULL, NULL, NULL, NULL, sset);
    if (ret != RTNORM)
        return;

    // --- 获取基点 ---
    ads_point basePt;
    if (acedGetPoint(NULL, _T("\n指定复制基点: "), basePt) != RTNORM) {
        acedSSFree(sset);
        return;
    }

    // --- 遍历选择集，处理视口 ---
    long ssLen;
    acedSSLength(sset, &ssLen);

    g_copiedViewportStates.clear();

    int vpCount = 0;
    int vpIndex = 0; // XData 索引计数器

    for (long i = 0; i < ssLen; i++) {
        ads_name en;
        acedSSName(sset, i, en);

        AcDbObjectId objId;
        acdbGetObjectId(objId, en);

        if (IsLayoutViewport(objId)) {
            // 收集冻结状态
            ViewportFreezeState state = CollectViewportFreezeState(objId);
            g_copiedViewportStates.push_back(state);

            // 附加 XData 标记（用于跨文档匹配）
            AttachIndexXData(objId, vpIndex);

            vpCount++;
            vpIndex++;
        }
    }

    // --- 复制到剪贴板 ---
    acedCopybase(basePt, sset);

    // --- 清除源视口的 XData 标记 ---
    for (long i = 0; i < ssLen; i++) {
        ads_name en;
        acedSSName(sset, i, en);

        AcDbObjectId objId;
        acdbGetObjectId(objId, en);

        if (IsLayoutViewport(objId)) {
            RemoveOurXData(objId);
        }
    }

    acedSSFree(sset);

    // --- 输出结果 ---
    acutPrintf(_T("\n✅ 已复制 %lld 个实体到剪贴板，"), ssLen);
    acutPrintf(_T("其中 %d 个视口携带冻结状态。"), vpCount);
}

//-----------------------------------------------------------------------------
// 命令：PasteVpFreeze
//   — 从剪贴板粘贴，自动识别新视口并恢复冻结状态
//-----------------------------------------------------------------------------
static void ACRX_CMD_ENTRYPOINT pasteVpFreeze()
{
    // --- 检查是否有可粘贴的冻结状态 ---
    if (g_copiedViewportStates.empty()) {
        acutPrintf(_T("\n⚠️ 未检测到已复制的视口冻结状态。"));
        acutPrintf(_T("\n   请先在源文档中使用 VpFreezeCopy 命令。"));
    }

    // --- 用户指定插入点 ---
    ads_point insPt;
    if (acedGetPoint(NULL, _T("\n指定粘贴插入点: "), insPt) != RTNORM)
        return;

    // --- 注册 XData 需要的 APPID（确保目标文档中有此注册） ---
    acdbRegApp(kAppName);

    // --- 从剪贴板粘贴 ---
    int pasteType = 0;
    if (acedPasteClip(&pasteType, insPt) != RTNORM) {
        // 即使无实体粘贴，也可能有纯数据
    }

    // --- 在目标文档中查找带有 VPFREECOPY XData 的视口 ---
    struct ViewportApplyInfo
    {
        int index;
        AcDbObjectId vpId;
    };
    std::vector<ViewportApplyInfo> taggedViewports;

    // 遍历当前空间中的所有实体
    AcDbBlockTable *pBlkTbl = nullptr;
    acdbCurDwg()->getBlockTable(pBlkTbl, AcDb::kForRead);

    AcDbBlockTableRecord *pBlkTblRec = nullptr;
    pBlkTbl->getAt(ACDB_MODEL_SPACE, pBlkTblRec, AcDb::kForRead);
    pBlkTbl->close();

    AcDbBlockTableRecordIterator *pIter = nullptr;
    pBlkTblRec->newIterator(pIter);

    for (; !pIter->done(); pIter->step()) {
        AcDbObjectId entId;
        pIter->getEntityId(entId);

        if (!IsLayoutViewport(entId))
            continue;

        int idx;
        if (ReadIndexXData(entId, idx)) {
            ViewportApplyInfo info;
            info.index = idx;
            info.vpId = entId;
            taggedViewports.push_back(info);
        }
    }

    delete pIter;
    pBlkTblRec->close();

    // --- 按 index 排序，确保顺序与应用匹配 ---
    // index 通常就是插入顺序，排序以防万一
    std::sort(taggedViewports.begin(), taggedViewports.end(),
        [](const ViewportApplyInfo &a, const ViewportApplyInfo &b) {
            return a.index < b.index;
        });

    // --- 应用冻结状态 ---
    int appliedCount = 0;
    int totalChanged = 0;
    int totalLocked = 0;

    for (auto &tv : taggedViewports) {
        if (tv.index >= 0 && tv.index < (int)g_copiedViewportStates.size()) {
            auto result = ApplyViewportFreezeState(tv.vpId,
                g_copiedViewportStates[tv.index]);
            totalChanged += result.changed;
            totalLocked += result.locked;
            appliedCount++;
        }
        // 清除 XData 标记
        RemoveOurXData(tv.vpId);
    }

    if (appliedCount > 0) {
        acutPrintf(_T("\n✅ 粘贴完成：已恢复 %d 个视口的冻结状态"), appliedCount);
        acutPrintf(_T("（更新 %d 个图层"), totalChanged);
        if (totalLocked > 0)
            acutPrintf(_T("，跳过 %d 个锁定图层"), totalLocked);
        acutPrintf(_T("）。"));
    } else {
        acutPrintf(_T("\n✅ 粘贴完成（未检测到带冻结状态的视口）。"));
    }
}

//-----------------------------------------------------------------------------
// ObjectARX 入口
//-----------------------------------------------------------------------------
extern "C" AcRx::AppRetCode
acrxEntryPoint(AcRx::AppMsgCode msg, void *pkt)
{
    switch (msg) {
    case AcRx::kInitAppMsg:
        acrxDynamicLinker->unlockApplication(pkt);
        acrxDynamicLinker->registerAppMDIAware(pkt);

        acedRegCmds->addCommand(
            _T("VPFREEZE_TOOLS"),
            _T("CopyVpFreeze"),
            _T("VpFreezeCopy"),
            ACRX_CMD_MODAL,
            copyVpFreeze
        );

        acedRegCmds->addCommand(
            _T("VPFREEZE_TOOLS"),
            _T("PasteVpFreeze"),
            _T("VpFreezePaste"),
            ACRX_CMD_MODAL,
            pasteVpFreeze
        );

        acutPrintf(_T("\n🔧 VpFreezeCopyPaste 已加载。"));
        acutPrintf(_T("\n   命令: VpFreezeCopy  — 框选实体→剪贴板（视口携带冻结状态）"));
        acutPrintf(_T("\n   命令: VpFreezePaste — 从剪贴板粘贴（自动恢复视口冻结状态）"));
        break;

    case AcRx::kUnloadAppMsg:
        acedRegCmds->removeGroup(_T("VPFREEZE_TOOLS"));
        acutPrintf(_T("\n🔧 VpFreezeCopyPaste 已卸载。"));
        break;

    default:
        break;
    }

    return AcRx::kRetOK;
}
