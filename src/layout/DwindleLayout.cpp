#include "DwindleLayout.hpp"
#include "../Compositor.hpp"

int CHyprDwindleLayout::getNodesOnMonitor(const int& id) {
    int no = 0;
    for (auto& n : m_lDwindleNodesData) {
        if (n.monitorID == id)
            ++no;
    }
    return no;
}

SDwindleNodeData* CHyprDwindleLayout::getFirstNodeOnMonitor(const int& id) {
    for (auto& n : m_lDwindleNodesData) {
        if (n.monitorID == id)
            return &n;
    }
    return nullptr;
}

SDwindleNodeData* CHyprDwindleLayout::getNodeFromWindow(CWindow* pWindow) {
    for (auto& n : m_lDwindleNodesData) {
        if (n.pWindow == pWindow)
            return &n;
    }

    return nullptr;
}

void CHyprDwindleLayout::applyNodeDataToWindow(SDwindleNodeData* pNode) {
    const auto PMONITOR = g_pCompositor->getMonitorFromID(pNode->monitorID);

    if (!PMONITOR){
        Debug::log(ERR, "Orphaned Node %x (monitor ID: %i)!!", pNode, pNode->monitorID);
        return;
    }

    // Don't set nodes, only windows.
    if (pNode->isNode) 
        return;

    // for gaps outer
    const bool DISPLAYLEFT          = STICKS(pNode->position.x, PMONITOR->vecPosition.x + PMONITOR->vecReservedTopLeft.x);
    const bool DISPLAYRIGHT         = STICKS(pNode->position.x + pNode->size.x, PMONITOR->vecPosition.x + PMONITOR->vecSize.x - PMONITOR->vecReservedBottomRight.x);
    const bool DISPLAYTOP           = STICKS(pNode->position.y, PMONITOR->vecPosition.y + PMONITOR->vecReservedTopLeft.y);
    const bool DISPLAYBOTTOM        = STICKS(pNode->position.y + pNode->size.y, PMONITOR->vecPosition.y + PMONITOR->vecSize.y - PMONITOR->vecReservedBottomRight.y);

    const auto BORDERSIZE           = g_pConfigManager->getInt("general:border_size");
    const auto GAPSIN               = g_pConfigManager->getInt("general:gaps_in");
    const auto GAPSOUT              = g_pConfigManager->getInt("general:gaps_out");

    const auto PWINDOW = pNode->pWindow;

    if (!g_pCompositor->windowValidMapped(PWINDOW)) {
        Debug::log(ERR, "Node %x holding invalid window %x!!", pNode, PWINDOW);
        return;
    }

    PWINDOW->m_vSize = pNode->size;
    PWINDOW->m_vPosition = pNode->position;

    PWINDOW->m_vEffectivePosition = PWINDOW->m_vPosition + Vector2D(BORDERSIZE, BORDERSIZE);
    PWINDOW->m_vEffectiveSize = PWINDOW->m_vSize - Vector2D(2 * BORDERSIZE, 2 * BORDERSIZE);

    const auto OFFSETTOPLEFT = Vector2D(DISPLAYLEFT ? GAPSOUT : GAPSIN,
                                        DISPLAYTOP ? GAPSOUT : GAPSIN);

    const auto OFFSETBOTTOMRIGHT = Vector2D(DISPLAYRIGHT ? GAPSOUT : GAPSIN,
                                            DISPLAYBOTTOM ? GAPSOUT : GAPSIN);

    PWINDOW->m_vEffectivePosition = PWINDOW->m_vEffectivePosition + OFFSETTOPLEFT;
    PWINDOW->m_vEffectiveSize = PWINDOW->m_vEffectiveSize - OFFSETTOPLEFT - OFFSETBOTTOMRIGHT;

    // TEMP: remove when anims added
    PWINDOW->m_vRealPosition = PWINDOW->m_vEffectivePosition;
    PWINDOW->m_vRealSize = PWINDOW->m_vEffectiveSize;
    g_pXWaylandManager->setWindowSize(PWINDOW, PWINDOW->m_vRealSize);
}

void CHyprDwindleLayout::onWindowCreated(CWindow* pWindow) {
    m_lDwindleNodesData.push_back(SDwindleNodeData());
    const auto PNODE = &m_lDwindleNodesData.back();

    if (pWindow->m_bIsFloating) {
        // handle floating, TODO:
        return;
    }

    // Populate the node with our window's data
    PNODE->monitorID = pWindow->m_iMonitorID;
    PNODE->pWindow = pWindow;
    PNODE->isNode = false;

    const auto PMONITOR = g_pCompositor->getMonitorFromID(PNODE->monitorID);

    // if it's the first, it's easy. Make it fullscreen.
    if (getNodesOnMonitor(pWindow->m_iMonitorID) == 1) {
        PNODE->position = PMONITOR->vecPosition + PMONITOR->vecReservedTopLeft;
        PNODE->size = PMONITOR->vecSize - PMONITOR->vecReservedTopLeft - PMONITOR->vecReservedBottomRight;

        applyNodeDataToWindow(PNODE);

        return;
    }

    // If it's not, get the last node.
    const auto PLASTFOCUS = getNodeFromWindow(g_pCompositor->m_pLastFocus);
    SDwindleNodeData* OPENINGON = PLASTFOCUS;
    if (PLASTFOCUS) {
        if (PLASTFOCUS->monitorID != PNODE->monitorID) {
            OPENINGON = getNodeFromWindow(g_pCompositor->vectorToWindow(g_pInputManager->getMouseCoordsInternal()));
        }
    } else {
        OPENINGON = getNodeFromWindow(g_pCompositor->vectorToWindow(g_pInputManager->getMouseCoordsInternal()));
    }

    if (!OPENINGON) {
        OPENINGON = getFirstNodeOnMonitor(PNODE->monitorID);
    }

    if (!OPENINGON) {
        Debug::log(ERR, "OPENINGON still null?????");
        return;
    }

    m_lDwindleNodesData.push_back(SDwindleNodeData());
    const auto NEWPARENT = &m_lDwindleNodesData.back();

    // make the parent have the OPENINGON's stats
    NEWPARENT->children[0] = OPENINGON;
    NEWPARENT->children[1] = PNODE;
    NEWPARENT->position = OPENINGON->position;
    NEWPARENT->size = OPENINGON->size;
    NEWPARENT->monitorID = OPENINGON->monitorID;
    NEWPARENT->isNode = true; // it is a node

    // and update the previous parent if it exists
    if (OPENINGON->pParent) {
        if (OPENINGON->pParent->children[0] == OPENINGON) {
            OPENINGON->pParent->children[0] == NEWPARENT;
        } else {
            OPENINGON->pParent->children[1] == NEWPARENT;
        }
    }

    // Update the children
    if (NEWPARENT->size.x > NEWPARENT->size.y) {
        // split sidey
        OPENINGON->position = NEWPARENT->position;
        OPENINGON->size = Vector2D(NEWPARENT->size.x / 2.f, NEWPARENT->size.y);
        PNODE->position = Vector2D(NEWPARENT->position.x + NEWPARENT->size.x / 2.f, NEWPARENT->position.y);
        PNODE->size = Vector2D(NEWPARENT->size.x / 2.f, NEWPARENT->size.y);
    } else {
        // split toppy bottomy
        OPENINGON->position = NEWPARENT->position;
        OPENINGON->size = Vector2D(NEWPARENT->size.x, NEWPARENT->size.y / 2.f);
        PNODE->position = Vector2D(NEWPARENT->position.x, NEWPARENT->position.y + NEWPARENT->size.y / 2.f);
        PNODE->size = Vector2D(NEWPARENT->size.x, NEWPARENT->size.y / 2.f);
    }

    OPENINGON->pParent = NEWPARENT;
    PNODE->pParent = NEWPARENT;

    applyNodeDataToWindow(PNODE);
    applyNodeDataToWindow(OPENINGON);
}

void CHyprDwindleLayout::onWindowRemoved(CWindow* pWindow) {
    if (!g_pCompositor->windowValidMapped(pWindow))
        return;

    const auto PNODE = getNodeFromWindow(pWindow);

    if (!PNODE)
        return;

    const auto PPARENT = PNODE->pParent;

    if (!PPARENT){
        m_lDwindleNodesData.remove(*PNODE);
        return;
    }

    const auto PSIBLING = PPARENT->children[0] == PNODE ? PPARENT->children[1] : PPARENT->children[0];

    PSIBLING->position = PPARENT->position;
    PSIBLING->size = PPARENT->size;
    PSIBLING->pParent = PPARENT->pParent;

    if (PPARENT->pParent != nullptr) {
        if (PPARENT->pParent->children[0] == PPARENT) {
            PPARENT->pParent->children[0] = PSIBLING;
        } else {
            PPARENT->pParent->children[1] = PSIBLING;
        }
    }

    m_lDwindleNodesData.remove(*PPARENT);
    m_lDwindleNodesData.remove(*PNODE);

    applyNodeDataToWindow(PSIBLING);
}