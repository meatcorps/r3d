/* r3d_animation_tree.c -- R3D Animation Tree Module.
 *
 * Copyright (c) 2025-2026 Le Juez Victor
 *
 * This software is provided 'as-is', without any express or implied warranty.
 * For conditions of distribution and use, see accompanying LICENSE file.
 */

#include <r3d/r3d_animation_tree.h>
#include <r3d_config.h>
#include <string.h>
#include <math.h>
#include <limits.h>

#include "./common/r3d_helper.h"
#include "./common/r3d_anim.h"

// ========================================
// TREE NODE TYPES
// ========================================

enum r3d_animtree_type {
    R3D_ANIMTREE_ANIM = 1,
    R3D_ANIMTREE_BLEND2,
    R3D_ANIMTREE_ADD2,
    R3D_ANIMTREE_SWITCH,
    R3D_ANIMTREE_STM,
    R3D_ANIMTREE_STM_X
};

typedef enum   r3d_animtree_type   r3d_animtree_type_t;
typedef struct r3d_animtree_base   r3d_animtree_base_t;
typedef struct r3d_animtree_anim   r3d_animtree_anim_t;
typedef struct r3d_animtree_blend2 r3d_animtree_blend2_t;
typedef struct r3d_animtree_add2   r3d_animtree_add2_t;
typedef struct r3d_animtree_switch r3d_animtree_switch_t;
typedef struct r3d_animtree_stm    r3d_animtree_stm_t;
typedef struct r3d_animtree_stm_x  r3d_animtree_stm_x_t;

union R3D_AnimationTreeNode {
    r3d_animtree_base_t*   base;
    r3d_animtree_anim_t*   anim;
    r3d_animtree_blend2_t* bln2;
    r3d_animtree_add2_t*   add2;
    r3d_animtree_switch_t* swch;
    r3d_animtree_stm_t*    stm;
    r3d_animtree_stm_x_t*  stmx;
};

// ========================================
// STATE MACHINE STRUCTURES
// ========================================

typedef struct {
    R3D_AnimationStmIndex beginIdx;
    R3D_AnimationStmIndex endIdx;
    float endWeight;
    R3D_StmEdgeParams params;
} r3d_stmedge_t;

typedef struct {
    int outCount;
    int maxOut;
    r3d_stmedge_t** outList;
    r3d_stmedge_t* activeIn;
} r3d_stmstate_t;

typedef struct {
    bool yes;
    float when;
} r3d_stmvisit_t;

// ========================================
// TREE NODE STRUCTURES
// ========================================

struct r3d_animtree_base {
    r3d_animtree_type_t type;
};

struct r3d_animtree_anim {
    r3d_animtree_type_t type;
    const R3D_Animation* animation;
    R3D_AnimationNodeParams params;
    struct {
        Transform last;
        Transform rest0;
        Transform restN;
        int loops;
    } root;
};

struct r3d_animtree_blend2 {
    r3d_animtree_type_t type;
    R3D_AnimationTreeNode inMain;
    R3D_AnimationTreeNode inBlend;
    R3D_Blend2NodeParams params;
};

struct r3d_animtree_add2 {
    r3d_animtree_type_t type;
    R3D_AnimationTreeNode inMain;
    R3D_AnimationTreeNode inAdd;
    R3D_Add2NodeParams params;
};

struct r3d_animtree_switch {
    r3d_animtree_type_t type;
    R3D_AnimationTreeNode* inList;
    float* inWeights;
    int inCount;
    int prevIn;
    float weightsInvSum;
    R3D_SwitchNodeParams params;
};

struct r3d_animtree_stm {
    r3d_animtree_type_t type;
    int statesCount;
    int edgesCount;
    int maxStates;
    int maxEdges;
    R3D_AnimationStmIndex activeIdx;
    R3D_AnimationTreeNode* nodeList;
    r3d_stmedge_t* edgeList;
    r3d_stmstate_t* stateList;
    r3d_stmvisit_t* visitList;
    struct {
        r3d_stmedge_t** edges;
        int idx;
        int len;
        r3d_stmedge_t** open;
        r3d_stmedge_t** next;
        bool* mark;
    } path;
};

struct r3d_animtree_stm_x {
    r3d_animtree_type_t type;
    R3D_AnimationTreeNode nested;
};

// ========================================
// TREE UPDATE/EVAL SUPPORT INFO STRUCTURES
// ========================================

typedef struct {
    bool anodeDone;
    float xFade;
    float consumedTime;
} upinfo_t;

typedef struct {
    Transform motion;
    Transform distance;
} rminfo_t;

// ========================================
// TREE NODE COMPUTE FUNCTION PROTOTYPES
// ========================================

static bool anode_update(const R3D_AnimationTree* atree, R3D_AnimationTreeNode anode,
                         float elapsedTime, upinfo_t* info);

static bool anode_eval(const R3D_AnimationTree* atree, R3D_AnimationTreeNode anode,
                       int boneIdx, Transform* out, rminfo_t* info);

static void anode_reset(R3D_AnimationTreeNode anode);

// ========================================
// BONE FUNCTIONS
// ========================================

static bool is_root_bone(const R3D_AnimationTree* atree, int boneIdx)
{
    return atree->rootBone == boneIdx;
}

static bool valid_root_bone(int boneIdx)
{
    return boneIdx >= 0;
}

static bool masked_bone(const R3D_BoneMask* bMask, int boneIdx)
{
    if (!bMask || boneIdx < 0 || boneIdx >= bMask->boneCount || boneIdx >= 256) return false;

    const int part = boneIdx / 32;
    const int bit = boneIdx % 32;

    return (bMask->mask[part] & (UINT32_C(1) << bit)) != 0;
}

// ========================================
// ANIMATION STATE MACHINE
// ========================================

static r3d_stmedge_t* stm_find_edge(const r3d_animtree_stm_t* node, const r3d_stmstate_t* state)
{
    int pathLen = node->path.len;
    int pathIdx = node->path.idx;

    if (pathIdx < pathLen)
    {
        return node->path.edges[pathIdx];
    }
    else
    {
        int edgesCount = state->outCount;
        for (int edgeIdx = 0; edgeIdx < edgesCount; edgeIdx++)
        {
            r3d_stmedge_t* edge = state->outList[edgeIdx];
            R3D_StmEdgeStatus status = edge->params.status;
            bool open = (status == R3D_STM_EDGE_AUTO || status == R3D_STM_EDGE_ONCE);
            if (open) return edge;
        }
    }

    return NULL;
}

static bool stm_update_edge(const R3D_AnimationTree* atree, r3d_animtree_stm_t* node,
                            r3d_stmedge_t* edge, float elapsedTime, float* consumedTime,
                            bool* done)
{
    float xFade = edge->params.xFadeTime;
    bool doXFade = (xFade > elapsedTime);

    if (doXFade)
    {
        float wInc = Remap(elapsedTime, 0.0f, xFade, 0.0f, 1.0f);
        edge->endWeight += wInc;

        float wClamp    = R3D_CLAMP(edge->endWeight, 0.0f, 1.0f);
        float wDelta    = edge->endWeight - wClamp;
        edge->endWeight = wClamp;

        *consumedTime = (wInc > 0.0f ? elapsedTime * (1.0f - wDelta/wInc) : elapsedTime);
        *done = FloatEquals(edge->endWeight, 1.0f);
    }
    else
    {
        edge->endWeight = 1.0f;
        *consumedTime = xFade;
        *done = true;
    }

    if (*done) return true;

    return anode_update(atree, node->nodeList[edge->beginIdx], elapsedTime, NULL);
}

static bool stm_next_state(r3d_animtree_stm_t* node, r3d_stmedge_t* edge,
                           bool edgeDone, bool nodeDone, R3D_AnimationStmIndex* nextIdx)
{
    R3D_StmEdgeMode mode = edge->params.mode;

    bool ready = ((mode == R3D_STM_EDGE_INSTANT && edgeDone) ||
                  (mode == R3D_STM_EDGE_ONDONE  && nodeDone));

    if (!ready) return false;

    R3D_AnimationStmIndex endIdx = edge->endIdx;
    r3d_stmstate_t* state = &node->stateList[endIdx];
    R3D_AnimationTreeNode anode = node->nodeList[endIdx];

    state->activeIn = edge;
    anode_reset(anode);

    edge->endWeight = 0.0f;
    if (edge->params.status == R3D_STM_EDGE_ONCE)
    {
        edge->params.status = edge->params.nextStatus;
    }

    *nextIdx = endIdx;

    return true;
}

static bool stm_update_state(const R3D_AnimationTree* atree, r3d_animtree_stm_t* node,
                             float elapsedTime, float* consumedTime,
                             R3D_AnimationStmIndex* activeIdx, bool* doNext, bool* stmDone)
{
    R3D_AnimationTreeNode* nodeList = node->nodeList;
    r3d_stmstate_t* state = &node->stateList[*activeIdx];

    bool edgeDone = true;
    float edgeTime = 0.0f;
    r3d_stmedge_t* activeEdge = state->activeIn;

    if (activeEdge)
    {
        if (!stm_update_edge(atree, node, activeEdge, elapsedTime, &edgeTime, &edgeDone))
        {
            return false;
        }
    }

    if (edgeDone) state->activeIn = NULL;

    r3d_stmedge_t* nextEdge = stm_find_edge(node, state);
    upinfo_t nodeInfo = {.xFade = (nextEdge ? nextEdge->params.xFadeTime : 0.0f)};
    if (!anode_update(atree, nodeList[*activeIdx], elapsedTime, &nodeInfo))
    {
        return false;
    }

    bool nodeDone = edgeDone && nodeInfo.anodeDone;
    bool isNext = (nextEdge ? stm_next_state(node, nextEdge, edgeDone, nodeDone, activeIdx) : false);

    *stmDone = (edgeDone && (nodeList[*activeIdx].base->type == R3D_ANIMTREE_STM_X));
    *doNext = nodeDone && isNext && !*stmDone;
    *consumedTime = R3D_MAX(edgeTime, nodeInfo.consumedTime);

    return true;
}

static int expand_path(r3d_animtree_stm_t* node, r3d_stmedge_t** edges, r3d_stmedge_t** next,
                       int nextCount, int pathLen, const r3d_stmstate_t* state)
{
    int maxOpenPaths = node->maxEdges;
    int maxPathLen = node->maxStates;
    int outCount = state->outCount;

    int added = 0;
    for (int edgeIdx = 0; edgeIdx < outCount; edgeIdx++)
    {
        r3d_stmedge_t* edge = state->outList[edgeIdx];

        bool closed = edge->params.status == R3D_STM_EDGE_OFF;
        if (closed) continue;

        if (nextCount < maxOpenPaths)
        {
            memcpy(&next[nextCount*maxPathLen], edges, pathLen * sizeof(r3d_stmedge_t*));
            next[nextCount*maxPathLen + pathLen] = edge;
            nextCount += 1;
            added += 1;
        }
        else
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to find path: max open paths count exceeded (%d)", maxOpenPaths);
            break;
        }
    }
    return added;
}

static bool stm_find_path(r3d_animtree_stm_t* node, R3D_AnimationStmIndex targetIdx)
{
    int maxPathLen = node->maxStates;
    r3d_stmedge_t** openPaths = node->path.open;
    r3d_stmedge_t** nextPaths = node->path.next;
    bool* marked = node->path.mark;

    memset(marked, false, node->statesCount * sizeof(*node->path.mark));
    marked[node->activeIdx] = true;

    int pathsCount = expand_path(
        node, openPaths, openPaths, 0, 0,
        &node->stateList[node->activeIdx]
    );

    int pathLen = 1;
    while(pathsCount > 0)
    {
        int nextCount = 0;
        for (int pIdx = 0; pIdx < pathsCount; pIdx++)
        {
            R3D_AnimationStmIndex stateIdx = openPaths[pIdx*maxPathLen + pathLen-1]->endIdx;
            if (stateIdx == targetIdx)
            {
                memcpy(node->path.edges, &openPaths[pIdx*maxPathLen], pathLen * sizeof(r3d_stmedge_t*));
                node->path.idx = 0;
                node->path.len = pathLen;
                return true;
            }
            if (marked[stateIdx]) continue;
            else marked[stateIdx] = true;

            nextCount += expand_path(
                node, &openPaths[pIdx*maxPathLen], nextPaths,
                nextCount, pathLen, &node->stateList[stateIdx]
            );
        }

        if (pathLen < maxPathLen)
        {
            pathLen += 1;
        }
        else
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to find path: max path lenght exceeded (%d)", maxPathLen);
            break;
        }

        memcpy(openPaths, nextPaths, maxPathLen * nextCount * sizeof(r3d_stmedge_t*));
        pathsCount = nextCount;
    }

    return false;
}

// ========================================
// TREE NODE CREATE FUNCTION
// ========================================

static R3D_AnimationTreeNode* anode_create(R3D_AnimationTree* atree, r3d_animtree_type_t type, size_t nodeSize)
{
    R3D_AnimationTreeNode* node;

    int poolSize = atree->nodePoolSize;
    if (poolSize < atree->nodePoolMaxSize)
    {
        node = &atree->nodePool[poolSize];
        node->base = r3d_zalloc(nodeSize);
        node->base->type = type;
    }
    else
    {
        return NULL;
    }

    atree->nodePoolSize += 1;
    return node;
}

// ========================================
// TREE NODE RESET FUNCTIONS
// ========================================

static void anode_reset_anim(r3d_animtree_anim_t* node)
{
    const R3D_Animation* a = node->animation;
    R3D_AnimationState* s = &node->params.state;

    bool forward = (s->speed >= 0.0f);
    float duration = a->duration / a->ticksPerSecond;
    s->currentTime = forward ? 0.0f : duration;

    node->root.last = forward ? node->root.rest0 : node->root.restN;
    node->root.loops = -1;
}

static void anode_reset_blend2(r3d_animtree_blend2_t* node)
{
    anode_reset(node->inMain);
    anode_reset(node->inBlend);
}

static void anode_reset_add2(r3d_animtree_add2_t* node)
{
    anode_reset(node->inMain);
    anode_reset(node->inAdd);
}

static void anode_reset_switch(r3d_animtree_switch_t* node)
{
    int inCount = node->inCount;
    int activeIn = node->params.activeInput;

    for (int i = 0; i < inCount; i++)
    {
        node->inWeights[i] = 0.0f;
    }
    node->inWeights[activeIn] = 1.0f;

    for (int i = 0; i < inCount; i++)
    {
        anode_reset(node->inList[i]);
    }
}

static void anode_reset_stm(r3d_animtree_stm_t* node)
{
    node->activeIdx = 0;
    node->path.idx = 0;
    node->path.len = 0;
    node->stateList[0].activeIn = NULL;

    anode_reset(node->nodeList[0]);
}

// other stm reset version: reset only active node+state
/*
static void anode_reset_stm(r3d_animtree_stm_t* node)
{
    int activeIdx = node->activeIdx;
    r3d_stmedge_t* edge = node->stateList[activeIdx].activeIn;
    if (edge) edge->endWeight = 0.0f;

    anode_reset(node->nodeList[activeIdx]);
}
*/

static void anode_reset_stm_x(r3d_animtree_stm_x_t* node)
{
    anode_reset(node->nested);
}

static void anode_reset(R3D_AnimationTreeNode anode)
{
    switch(anode.base->type)
    {
    case R3D_ANIMTREE_ANIM:   anode_reset_anim(anode.anim); break;
    case R3D_ANIMTREE_BLEND2: anode_reset_blend2(anode.bln2); break;
    case R3D_ANIMTREE_ADD2:   anode_reset_add2(anode.add2); break;
    case R3D_ANIMTREE_SWITCH: anode_reset_switch(anode.swch); break;
    case R3D_ANIMTREE_STM:    anode_reset_stm(anode.stm); break;
    case R3D_ANIMTREE_STM_X:  anode_reset_stm_x(anode.stmx); break;
    default:
        R3D_TRACELOG(LOG_WARNING, "Failed to reset node: invalid type %d", anode.base->type);
        break;
    }
}

// ========================================
// TREE NODE UPDATE FUNCTIONS
// ========================================

static bool anode_update_anim(r3d_animtree_anim_t* node, float elapsedTime, upinfo_t* info)
{
    const R3D_Animation* a = node->animation;
    R3D_AnimationState* s = &node->params.state;

    if (!s->play)
    {
        node->root.loops = -1;

        if (info)
        {
            *info = (upinfo_t) {
                .anodeDone = true,
                .xFade = info->xFade,
                .consumedTime = 0.0f
            };
        }
        return true;
    }

    float speed = s->speed;
    if (!isfinite(speed) || !isfinite(elapsedTime)) return false;

    float duration = (a->ticksPerSecond > 0.0f) ? a->duration / a->ticksPerSecond : 0.0f;

    if (!isfinite(duration) || duration <= 0.0f)
    {
        s->currentTime = 0.0f;
        s->play = false;
        node->root.loops = -1;

        if (info)
        {
            *info = (upinfo_t) {
                .anodeDone = true,
                .xFade = info->xFade,
                .consumedTime = 0.0f
            };
        }
        return true;
    }

    double nextTime = (double)s->currentTime + (double)speed * elapsedTime;

    bool cross = elapsedTime > 0.0f &&
                 ((speed < 0.0f && nextTime <= 0.0) ||
                  (speed > 0.0f && nextTime >= duration));

    node->root.loops = -1;

    if (cross)
    {
        if (s->loop)
        {
            double crossings = (speed > 0.0f)
                ? floor(nextTime / duration)
                : floor(-nextTime / duration) + 1.0;

            double wrappedTime = fmod(nextTime, (double)duration);
            if (wrappedTime < 0.0) wrappedTime += duration;
            if (speed < 0.0f && wrappedTime == 0.0) wrappedTime = duration;

            s->currentTime = (float)wrappedTime;

            // The first crossing is handled by the root-motion split.
            // Additional complete cycles are accumulated separately.
            node->root.loops = (crossings >= INT_MAX) ? INT_MAX - 1 : (int)crossings - 1;
        }
        else
        {
            double boundaryTime = (speed > 0.0f)
                ? ((double)duration - s->currentTime) / speed
                : (double)s->currentTime / -speed;

            elapsedTime = R3D_CLAMP((float)boundaryTime, 0.0f, elapsedTime);
            s->currentTime = R3D_CLAMP((float)nextTime, 0.0f, duration);
            s->play = false;
        }
    }
    else
    {
        s->currentTime = (float)nextTime;
    }

    if (info)
    {
        float xFade = info->xFade * fabsf(speed);
        float durXFade = R3D_CLAMP(duration - xFade, 0.0f, duration);
        bool crossXFade = (speed < 0.0f && nextTime <= xFade) ||
                          (speed > 0.0f && nextTime >= durXFade);

        *info = (upinfo_t) {
            .anodeDone = crossXFade ? node->params.looper : false,
            .xFade = xFade,
            .consumedTime = !FloatEquals(speed, 0.0f) ? elapsedTime : 0.0f
        };
    }

    return true;
}

static bool anode_update_blend2(const R3D_AnimationTree* atree, r3d_animtree_blend2_t* node, float elapsedTime, upinfo_t* info)
{
    return (
        anode_update(atree, node->inMain, elapsedTime, info) &&
        anode_update(atree, node->inBlend, elapsedTime, NULL)
    );
}

static bool anode_update_add2(const R3D_AnimationTree* atree, r3d_animtree_add2_t* node, float elapsedTime, upinfo_t* info)
{
    return (
        anode_update(atree, node->inMain, elapsedTime, info) &&
        anode_update(atree, node->inAdd, elapsedTime, NULL)
    );
}

static bool anode_update_switch(const R3D_AnimationTree* atree, r3d_animtree_switch_t* node, float elapsedTime, upinfo_t* info)
{
    int inCount = node->inCount;
    int activeIn = node->params.activeInput;
    if (activeIn < 0 || activeIn >= inCount)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to update switch: active input %d out of range (%d)", activeIn, inCount);
        return false;
    }

    bool reset = (activeIn == node->prevIn) ? false : !node->params.synced;
    if (reset) anode_reset(node->inList[activeIn]);

    for (int i = 0; i < inCount; i++)
    {
        if (!anode_update(atree, node->inList[i], elapsedTime, (i == activeIn) ? info : NULL))
        {
            return false;
        }
    }
    node->prevIn = activeIn;

    float xFade = node->params.xFadeTime;
    bool noXFade = (xFade <= elapsedTime);

    if (noXFade)
    {
        for (int i = 0; i < inCount; i++)
        {
            node->inWeights[i] = 0.0f;
        }
        node->inWeights[activeIn] = 1.0f;
    }
    else
    {
        float wFade = Remap(elapsedTime, 0.0f, xFade, 0.0f, 1.0f);
        for (int i = 0; i < inCount; i++)
        {
            float wSign = (i == activeIn) ? 1.0f : -1.0f;
            node->inWeights[i] = R3D_CLAMP(node->inWeights[i] + wSign*wFade, 0.0f, 1.0f);
        }
    }

    float wSum = 0.0f;
    for (int i = 0; i < inCount; i++)
    {
        wSum += node->inWeights[i];
    }
    node->weightsInvSum = 1.0f / wSum;

    return true;
}

static bool anode_update_stm(const R3D_AnimationTree* atree, r3d_animtree_stm_t* node, float elapsedTime, upinfo_t* info)
{
    R3D_AnimationStmIndex activeIdx = node->activeIdx;

    r3d_stmvisit_t* visited = node->visitList;
    memset(visited, 0, node->statesCount * sizeof(*node->visitList));

    float startTime = elapsedTime;
    bool doNext = true;
    bool stmDone = false;

    while(doNext)
    {
        if (visited[activeIdx].yes && FloatEquals(visited[activeIdx].when, elapsedTime))
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to update stm: cycle detected, aborted");
            return false;
        }
        visited[activeIdx] = (r3d_stmvisit_t){.yes = true, .when = elapsedTime};

        float consumedTime;
        if (!stm_update_state(atree, node, elapsedTime, &consumedTime, &activeIdx, &doNext, &stmDone))
        {
            return false;
        }
        elapsedTime -= consumedTime;

        if (doNext)
        {
            int pathLen = node->path.len;
            int pathIdx = node->path.idx;
            if (pathIdx < pathLen) node->path.idx += 1;
        }

        if (FloatEquals(elapsedTime, 0.0f))
        {
            break;
        }

        if (elapsedTime < 0.0f)
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to update stm: incorrect time calculation (%f)", elapsedTime);
            return false;
        }
    }

    node->activeIdx = activeIdx;

    if (info)
    {
        *info = (upinfo_t) {
            .anodeDone    = stmDone,
            .consumedTime = startTime - elapsedTime
        };
    }

    return true;
}

static bool anode_update_stm_x(const R3D_AnimationTree* atree, r3d_animtree_stm_x_t* node, float elapsedTime, upinfo_t* info)
{
    return anode_update(atree, node->nested, elapsedTime, info);
}

static bool anode_update(const R3D_AnimationTree* atree, R3D_AnimationTreeNode anode, float elapsedTime, upinfo_t* info)
{
    switch(anode.base->type)
    {
    case R3D_ANIMTREE_ANIM:   return anode_update_anim(anode.anim, elapsedTime, info);
    case R3D_ANIMTREE_BLEND2: return anode_update_blend2(atree, anode.bln2, elapsedTime, info);
    case R3D_ANIMTREE_ADD2:   return anode_update_add2(atree, anode.add2, elapsedTime, info);
    case R3D_ANIMTREE_SWITCH: return anode_update_switch(atree, anode.swch, elapsedTime, info);
    case R3D_ANIMTREE_STM:    return anode_update_stm(atree, anode.stm, elapsedTime, info);
    case R3D_ANIMTREE_STM_X:  return anode_update_stm_x(atree, anode.stmx, elapsedTime, info);
    default:
        R3D_TRACELOG(LOG_WARNING, "Failed to update animation tree: invalid node type %d", anode.base->type);
        break;
    }
    return false;
}

// ========================================
// TREE NODE EVAL FUNCTIONS
// ========================================

static bool anode_eval_anim(const R3D_AnimationTree* atree, r3d_animtree_anim_t* node, int boneIdx, Transform* out, rminfo_t* info)
{
    const R3D_Animation* anim = node->animation;
    const R3D_AnimationState state = node->params.state;
    const R3D_AnimationChannel* channel = r3d_anim_channel_find(anim, boneIdx);

    if (channel)
    {
        *out = r3d_anim_channel_lerp(channel, state.currentTime * anim->ticksPerSecond, &atree->player.skeleton.localBind[boneIdx], NULL, NULL);
    }
    else
    {
        r3d_matrix_decompose(atree->player.skeleton.localBind[boneIdx], &out->translation, &out->rotation, &out->scale);
    }

    if (node->params.evalCallback)
    {
        node->params.evalCallback(anim, state, boneIdx, out, node->params.evalUserData);
    }

    if (channel && is_root_bone(atree, boneIdx))
    {
        if (info)
        {
            Transform motion = {0};
            const int loops = node->root.loops;

            if (loops > 0)
            {
                Transform cycle = (state.speed >= 0.0f)
                    ? r3d_anim_transform_subtr(node->root.restN, node->root.rest0)
                    : r3d_anim_transform_subtr(node->root.rest0, node->root.restN);

                motion = r3d_anim_transform_scale(cycle, (float)loops);
            }

            if (loops >= 0)
            {
                const bool forward = state.speed > 0.0f;
                const Transform rest0 = forward ? node->root.rest0 : node->root.restN;
                const Transform restN = forward ? node->root.restN : node->root.rest0;
                const Transform split = r3d_anim_transform_add(
                    r3d_anim_transform_subtr(restN, node->root.last),
                    r3d_anim_transform_subtr(*out, rest0)
                );
                motion = r3d_anim_transform_add(motion, split);
                motion.rotation = QuaternionNormalize(motion.rotation);
                info->motion = motion;
            }
            else
            {
                info->motion = r3d_anim_transform_subtr(*out, node->root.last);
            }

            info->distance = r3d_anim_transform_subtr(*out, node->root.rest0);
        }

        node->root.last = *out;
    }

    return true;
}

static bool anode_eval_blend2(const R3D_AnimationTree* atree, r3d_animtree_blend2_t* node, int boneIdx, Transform* out, rminfo_t* info)
{
    const R3D_BoneMask* bmask = node->params.boneMask;
    const bool doBlend = !bmask || masked_bone(bmask, boneIdx);
    const bool isRm = info && is_root_bone(atree, boneIdx);

    rminfo_t rm[2] = {0};
    Transform in[2] = {0};

    bool success = anode_eval(atree, node->inMain, boneIdx, &in[0], isRm ? &rm[0] : NULL);
    if (!success)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to eval blend2 node");
        return false;
    }

    if (doBlend)
    {
        success = anode_eval(atree, node->inBlend, boneIdx, &in[1], isRm ? &rm[1] : NULL);
        if (!success)
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to eval blend2 node");
            return false;
        }
    }

    const float w = R3D_CLAMP(node->params.blend, 0.0f, 1.0f);
    *out = doBlend ? r3d_anim_transform_lerp(in[0], in[1], w) : in[0];

    if (isRm)
    {
        *info = doBlend ? (rminfo_t) {
            .motion = r3d_anim_transform_lerp(rm[0].motion, rm[1].motion, w),
            .distance = r3d_anim_transform_lerp(rm[0].distance, rm[1].distance, w)
        } : rm[0];
    }

    return true;
}

static bool anode_eval_add2(const R3D_AnimationTree* atree, r3d_animtree_add2_t* node, int boneIdx, Transform* out, rminfo_t* info)
{
    const R3D_BoneMask* bmask = node->params.boneMask;
    const bool doAdd = !bmask || masked_bone(bmask, boneIdx);
    const bool isRm = info && is_root_bone(atree, boneIdx);

    rminfo_t mainRm = {0};
    Transform in[2] = {0};

    bool success = anode_eval(atree, node->inMain, boneIdx, &in[0], isRm ? &mainRm : NULL);
    if (!success)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to eval add2 node");
        return false;
    }

    if (doAdd)
    {
        success = anode_eval(atree, node->inAdd, boneIdx, &in[1], NULL);
        if (!success)
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to eval add2 node");
            return false;
        }
    }

    const float w = R3D_CLAMP(node->params.weight, 0.0f, 1.0f);

    if (doAdd && w > 0.0f)
    {
        Transform reference;
        r3d_matrix_decompose(atree->player.skeleton.localBind[boneIdx], &reference.translation, &reference.rotation, &reference.scale);

        *out = r3d_anim_transform_additive(in[0], in[1], reference, w);
    }
    else
    {
        *out = in[0];
    }

    if (isRm) *info = mainRm;

    return true;
}

static bool anode_eval_switch(const R3D_AnimationTree* atree, r3d_animtree_switch_t* node, int boneIdx, Transform* out, rminfo_t* info)
{
    const bool isRm = info && is_root_bone(atree, boneIdx);
    const float wInvSum = node->weightsInvSum;

    rminfo_t rm = {0};
    Transform result = {0};

    for (int i = 0; i < node->inCount; i++)
    {
        const float w = node->inWeights[i] * wInvSum;
        if (FloatEquals(w, 0.0f)) continue;

        rminfo_t inRm = {0};
        Transform inTr = {0};

        bool success = anode_eval(atree, node->inList[i], boneIdx, &inTr, isRm ? &inRm : NULL);
        if (!success)
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to eval switch node: input %d failed", i);
            return false;
        }

        result = r3d_anim_transform_addx_v(result, inTr, w);

        if (isRm)
        {
            rm.motion = r3d_anim_transform_addx_v(rm.motion, inRm.motion, w);
            rm.distance = r3d_anim_transform_addx_v(rm.distance, inRm.distance, w);
        }
    }

    *out = result;
    if (isRm) *info = rm;

    return true;
}

static bool anode_eval_stm(const R3D_AnimationTree* atree, r3d_animtree_stm_t* node, int boneIdx, Transform* out, rminfo_t* info)
{
    const R3D_AnimationStmIndex activeIdx = node->activeIdx;
    const bool isRm = info && is_root_bone(atree, boneIdx);

    rminfo_t activeRm = {0};
    Transform activeTr = {0};

    bool success = anode_eval(atree, node->nodeList[activeIdx], boneIdx, &activeTr, isRm ? &activeRm : NULL);
    if (!success)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to eval stm state %d", activeIdx);
        return false;
    }

    const r3d_stmedge_t* edge = node->stateList[activeIdx].activeIn;

    if (!edge)
    {
        *out = activeTr;
        if (isRm) *info = activeRm;
        return true;
    }

    rminfo_t  edgeRm = {0};
    Transform edgeTr = {0};

    success = anode_eval(atree, node->nodeList[edge->beginIdx], boneIdx, &edgeTr, isRm ? &edgeRm : NULL);
    if (!success)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to eval stm state %d", edge->beginIdx);
        return false;
    }

    const float endWeight = R3D_CLAMP(edge->endWeight, 0.0f, 1.0f);
    *out = r3d_anim_transform_lerp(edgeTr, activeTr, endWeight);

    if (isRm)
    {
        *info = (rminfo_t) {
            .motion = r3d_anim_transform_lerp(edgeRm.motion, activeRm.motion, endWeight),
            .distance = r3d_anim_transform_lerp(edgeRm.distance, activeRm.distance, endWeight)
        };
    }

    return true;
}

static bool anode_eval_stm_x(const R3D_AnimationTree* atree, r3d_animtree_stm_x_t* node, int boneIdx, Transform* out, rminfo_t* info)
{
    return anode_eval(atree, node->nested, boneIdx, out, info);
}

static bool anode_eval(const R3D_AnimationTree* atree, R3D_AnimationTreeNode anode, int boneIdx, Transform* out, rminfo_t* info)
{
    switch(anode.base->type)
    {
    case R3D_ANIMTREE_ANIM:   return anode_eval_anim(atree, anode.anim, boneIdx, out, info);
    case R3D_ANIMTREE_BLEND2: return anode_eval_blend2(atree, anode.bln2, boneIdx, out, info);
    case R3D_ANIMTREE_ADD2:   return anode_eval_add2(atree, anode.add2, boneIdx, out, info);
    case R3D_ANIMTREE_SWITCH: return anode_eval_switch(atree, anode.swch, boneIdx, out, info);
    case R3D_ANIMTREE_STM:    return anode_eval_stm(atree, anode.stm, boneIdx, out, info);
    case R3D_ANIMTREE_STM_X:  return anode_eval_stm_x(atree, anode.stmx, boneIdx, out, info);
    default:
        R3D_TRACELOG(LOG_WARNING, "Failed to eval animation tree: invalid node type %d", anode.base->type);
        break;
    }
    return false;
}


/**
 * Evaluate one morph-target weight through an animation
 * tree node.
 *
 * The caller supplies the target's imported default weight.
 * A clip that does not animate the mesh contributes that
 * default instead of leaving stale weights behind.
 *
 * This is allocation-free and does not modify node state.
 */
static bool anode_eval_morph(R3D_AnimationTreeNode node, int meshIndex, int targetIndex, int targetCount, float defaultWeight, float* out)
{
    if (!node.base || !out)
        return false;

    switch (node.base->type)
    {
        // ========================================
        // ANIMATION LEAF
        // ========================================

        case R3D_ANIMTREE_ANIM:
        {
            const r3d_animtree_anim_t* anim = node.anim;

            if (!anim->animation)
                return false;

            const R3D_Animation* clip = anim->animation;

            const R3D_MorphAnimationChannel* channel =
                R3D_GetMorphAnimationChannel(
                    clip,
                    meshIndex
                );

            // This clip doesn't animate this mesh.
            if (!channel)
            {
                *out = defaultWeight;
                return true;
            }

            // Wrong model/animation combination.
            if (channel->targetCount != targetCount)
                return false;

            const float timeTicks =
                anim->params.state.currentTime *
                clip->ticksPerSecond;

            return R3D_SampleMorphAnimationWeight(
                clip,
                meshIndex,
                targetIndex,
                timeTicks,
                defaultWeight,
                out
            );
        }

        // ========================================
        // BLEND2
        // ========================================

        case R3D_ANIMTREE_BLEND2:
        {
            const r3d_animtree_blend2_t* blend = node.bln2;

            float a = 0.0f;
            float b = 0.0f;

            if (!anode_eval_morph(
                blend->inMain,
                meshIndex, targetIndex, targetCount,
                defaultWeight, &a))
            {
                return false;
            }

            if (!anode_eval_morph(
                blend->inBlend,
                meshIndex, targetIndex, targetCount,
                defaultWeight, &b))
            {
                return false;
            }

            const float weight =
                R3D_CLAMP(blend->params.blend, 0.0f, 1.0f);

            *out = a + (b - a) * weight;

            return isfinite(*out);
        }

        // ========================================
        // ADD2
        // ========================================

        case R3D_ANIMTREE_ADD2:
        {
            const r3d_animtree_add2_t* add = node.add2;

            float mainWeight = 0.0f;
            float additiveWeight = 0.0f;

            if (!anode_eval_morph(
                add->inMain,
                meshIndex, targetIndex, targetCount,
                defaultWeight, &mainWeight))
            {
                return false;
            }

            if (!anode_eval_morph(
                add->inAdd,
                meshIndex, targetIndex, targetCount,
                defaultWeight, &additiveWeight))
            {
                return false;
            }

            const float weight =
                R3D_CLAMP(add->params.weight, 0.0f, 1.0f);

            // Add relative to the imported rest weight.
            // This prevents a nonzero default from being
            // added twice.
            *out = mainWeight +
                (additiveWeight - defaultWeight) * weight;

            return isfinite(*out);
        }

        // ========================================
        // SWITCH
        // ========================================

        case R3D_ANIMTREE_SWITCH:
        {
            const r3d_animtree_switch_t* sw = node.swch;

            if (!sw->inWeights || !sw->inList ||
                sw->inCount <= 0 ||
                !isfinite(sw->weightsInvSum) ||
                sw->weightsInvSum <= 0.0f)
            {
                return false;
            }

            float result = 0.0f;
            bool hasContribution = false;

            for (int i = 0; i < sw->inCount; i++)
            {
                const float weight =
                    sw->inWeights[i] * sw->weightsInvSum;

                if (!isfinite(weight))
                    return false;

                if (weight == 0.0f)
                    continue;

                float value = 0.0f;

                if (!anode_eval_morph(
                    sw->inList[i],
                    meshIndex, targetIndex, targetCount,
                    defaultWeight, &value))
                {
                    return false;
                }

                result += value * weight;
                hasContribution = true;
            }

            if (!hasContribution)
                return false;

            *out = result;

            return isfinite(*out);
        }

        // ========================================
        // STATE MACHINE
        // ========================================

        case R3D_ANIMTREE_STM:
        {
            const r3d_animtree_stm_t* stm = node.stm;

            if (!stm->nodeList || !stm->stateList ||
                stm->activeIdx < 0 ||
                stm->activeIdx >= stm->statesCount)
            {
                return false;
            }

            const int activeIndex = stm->activeIdx;

            float activeValue = 0.0f;

            if (!anode_eval_morph(
                stm->nodeList[activeIndex],
                meshIndex, targetIndex, targetCount,
                defaultWeight, &activeValue))
            {
                return false;
            }

            const r3d_stmedge_t* edge =
                stm->stateList[activeIndex].activeIn;

            if (!edge)
            {
                *out = activeValue;
                return true;
            }

            if (edge->beginIdx < 0 ||
                edge->beginIdx >= stm->statesCount)
            {
                return false;
            }

            float previousValue = 0.0f;

            if (!anode_eval_morph(
                stm->nodeList[edge->beginIdx],
                meshIndex, targetIndex, targetCount,
                defaultWeight, &previousValue))
            {
                return false;
            }

            const float weight =
                R3D_CLAMP(edge->endWeight, 0.0f, 1.0f);

            *out = previousValue +
                (activeValue - previousValue) * weight;

            return isfinite(*out);
        }

        // ========================================
        // STATE MACHINE EXIT
        // ========================================

        case R3D_ANIMTREE_STM_X:
        {
            return anode_eval_morph(
                node.stmx->nested,
                meshIndex, targetIndex, targetCount,
                defaultWeight, out
            );
        }

        default:
            return false;
    }
}

/**
 * Evaluate a complete shape-key instance from the
 * current AnimationTree state.
 */
bool R3D_EvaluateAnimationTreeMorphs(const R3D_AnimationTree* tree, int meshIndex, R3D_ShapeKeyInstance* instance)
{
    if (!tree || !tree->rootNode ||
        !tree->rootNode->base ||
        meshIndex < 0 ||
        !instance ||
        !instance->source ||
        !instance->weights ||
        !instance->source->keys)
    {
        return false;
    }

    const int targetCount =
        instance->source->keyCount;

    if (targetCount <= 0)
        return false;

    const R3D_AnimationLib* library =
        &tree->player.animLib;

    // Does this library animate morph targets on
    // this mesh at all?
    //
    // If not, preserve manually controlled weights.
    bool hasMorphTracks = false;

    for (int i = 0; i < library->count; i++)
    {
        const R3D_MorphAnimationChannel* channel =
            R3D_GetMorphAnimationChannel(
                &library->animations[i],
                meshIndex
            );

        if (!channel)
            continue;

        // Prevent applying channels from an
        // incompatible mesh or model.
        if (channel->targetCount != targetCount)
            return false;

        hasMorphTracks = true;
    }

    if (!hasMorphTracks)
        return true;

    // Evaluate the tree for every target independently.
    // No scratch buffers or allocations per frame.
    for (int i = 0; i < targetCount; i++)
    {
        float weight = 0.0f;

        const float defaultWeight =
            instance->source->keys[i].defaultWeight;

        if (!anode_eval_morph(
            *tree->rootNode,
            meshIndex,
            i,
            targetCount,
            defaultWeight,
            &weight))
        {
            return false;
        }

        if (!R3D_SetShapeKeyWeight(
            instance, i, weight))
        {
            return false;
        }
    }

    return true;
}

// ========================================
// INTERNAL ANIMATION TREE FUNCTIONS
// ========================================

static bool atree_blend2_add(r3d_animtree_blend2_t* parent, R3D_AnimationTreeNode anode, int inIdx)
{
    switch(inIdx)
    {
    case 0: parent->inMain = anode; return true;
    case 1: parent->inBlend = anode; return true;
    default:
        R3D_TRACELOG(LOG_WARNING, "Failed to add node into blend2: invalid input index %d", inIdx);
        break;
    };
    return false;
}

static bool atree_add2_add(r3d_animtree_add2_t* parent, R3D_AnimationTreeNode anode, int inIdx)
{
    switch(inIdx)
    {
    case 0: parent->inMain = anode; return true;
    case 1: parent->inAdd = anode; return true;
    default:
        R3D_TRACELOG(LOG_WARNING, "Failed to add node into add2: invalid input index %d", inIdx);
    };
    return false;
}

static bool atree_switch_add(r3d_animtree_switch_t* parent, R3D_AnimationTreeNode anode, int inIdx)
{
    if (inIdx < parent->inCount)
    {
        parent->inList[inIdx] = anode;
        return true;
    }
    R3D_TRACELOG(LOG_WARNING, "Failed to add node into switch: invalid input index %d", inIdx);
    return false;
}

static R3D_AnimationTreeNode* atree_anim_create(R3D_AnimationTree* atree, R3D_AnimationNodeParams params, bool reset)
{
    const R3D_Animation* a = R3D_GetAnimation(atree->player.animLib, params.name);
    if (!a)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to create animation node: animation \"%s\" not found", params.name);
        return NULL;
    }
    R3D_AnimationTreeNode* anode = anode_create(atree, R3D_ANIMTREE_ANIM, sizeof(r3d_animtree_anim_t));
    if (!anode) return NULL;

    r3d_animtree_anim_t* anim = anode->anim;
    anim->animation = a;
    anim->params = params;
    if (reset) anode_reset_anim(anim);

    int boneIdx = atree->rootBone;
    if (valid_root_bone(boneIdx))
    {
        const R3D_AnimationState* s = &anim->params.state;
        const R3D_AnimationChannel* c = r3d_anim_channel_find(a, boneIdx);
        if (c != NULL)
        {
            anim->root.last = r3d_anim_channel_lerp(c, s->currentTime * a->ticksPerSecond, &atree->player.skeleton.localBind[boneIdx], &anim->root.rest0, &anim->root.restN);
        }
        else
        {
            Transform bind;
            r3d_matrix_decompose(
                atree->player.skeleton.localBind[boneIdx],
                &bind.translation, &bind.rotation, &bind.scale
            );
            anim->root.last = bind;
            anim->root.rest0 = bind;
            anim->root.restN = bind;
        }
        anim->root.loops = -1;
    }

    return anode;
}

static R3D_AnimationTreeNode* atree_blend2_create(R3D_AnimationTree* atree, R3D_Blend2NodeParams params)
{
    R3D_AnimationTreeNode* anode = anode_create(
        atree, R3D_ANIMTREE_BLEND2,
        sizeof(r3d_animtree_blend2_t)
    );
    if (!anode) return NULL;

    anode->bln2->params = params;
    return anode;
}

static R3D_AnimationTreeNode* atree_add2_create(R3D_AnimationTree* atree, R3D_Add2NodeParams params)
{
    R3D_AnimationTreeNode* anode = anode_create(
        atree, R3D_ANIMTREE_ADD2,
        sizeof(r3d_animtree_add2_t)
    );
    if (!anode) return NULL;

    anode->add2->params = params;
    return anode;
}

static R3D_AnimationTreeNode* atree_switch_create(R3D_AnimationTree* atree, int inCount, R3D_SwitchNodeParams params)
{
    R3D_AnimationTreeNode* anode = anode_create(
        atree, R3D_ANIMTREE_SWITCH,
        sizeof(r3d_animtree_switch_t)
    );
    if (!anode) return NULL;

    r3d_animtree_switch_t* swch = anode->swch;
    swch->inList    = r3d_zalloc(inCount * sizeof(*swch->inList));
    swch->inWeights = r3d_zalloc(inCount * sizeof(*swch->inWeights));
    swch->inCount   = inCount;
    swch->params    = params;

    swch->inWeights[params.activeInput] = 1.0f;
    return anode;
}

static R3D_AnimationTreeNode* atree_stm_create(R3D_AnimationTree* atree, int statesCount, int edgesCount, bool travel)
{
    R3D_AnimationTreeNode* anode = anode_create(atree, R3D_ANIMTREE_STM, sizeof(r3d_animtree_stm_t));
    if (!anode) return NULL;

    r3d_animtree_stm_t* stm = anode->stm;
    stm->nodeList  = r3d_zalloc(statesCount * sizeof(*stm->nodeList));
    stm->edgeList  = r3d_zalloc(edgesCount * sizeof(*stm->edgeList));
    stm->stateList = r3d_zalloc(statesCount * sizeof(*stm->stateList));
    stm->visitList = r3d_zalloc(statesCount * sizeof(*stm->visitList));
    stm->maxStates = statesCount;
    stm->maxEdges  = edgesCount;
    if (travel)
    {
        stm->path.edges = r3d_zalloc(statesCount * sizeof(*stm->path.edges));
        stm->path.open  = r3d_zalloc(edgesCount * statesCount * sizeof(*stm->path.open));
        stm->path.next  = r3d_zalloc(edgesCount * statesCount * sizeof(*stm->path.next));
        stm->path.mark  = r3d_zalloc(statesCount * sizeof(*stm->path.mark));
    }
    return anode;
}

static R3D_AnimationTreeNode* atree_stm_x_create(R3D_AnimationTree* atree, R3D_AnimationTreeNode nested)
{
    R3D_AnimationTreeNode* anode = anode_create(atree, R3D_ANIMTREE_STM_X, sizeof(r3d_animtree_stm_x_t));
    if (!anode) return NULL;

    anode->stmx->nested = nested;
    return anode;
}

static R3D_AnimationStmIndex atree_state_create(r3d_animtree_stm_t* node, R3D_AnimationTreeNode anode, int edgesCount)
{
    R3D_AnimationStmIndex nextIdx = node->statesCount;
    if (nextIdx >= node->maxStates) return -1;

    r3d_stmstate_t* state = &node->stateList[nextIdx];
    *state = (r3d_stmstate_t) {
        .outList = (edgesCount > 0 ? r3d_zalloc(edgesCount * sizeof(*state->outList)) : NULL),
        .outCount = 0,
        .maxOut = edgesCount,
        .activeIn = NULL
    };

    node->nodeList[nextIdx] = anode;
    node->statesCount += 1;

    return nextIdx;
}

static R3D_AnimationStmIndex atree_edge_create(r3d_animtree_stm_t* node, R3D_AnimationStmIndex beginIdx,
                                               R3D_AnimationStmIndex endIdx, R3D_StmEdgeParams params)
{
    R3D_AnimationStmIndex nextIdx = node->edgesCount;
    if (nextIdx >= node->maxEdges) return -1;

    r3d_stmedge_t* edge = &node->edgeList[nextIdx];
    *edge = (r3d_stmedge_t) {
        .beginIdx = beginIdx,
        .endIdx = endIdx,
        .endWeight = 0.0f,
        .params = params
    };

    r3d_stmstate_t* beginState = &node->stateList[beginIdx];
    int outCount = beginState->outCount;
    if (outCount >= beginState->maxOut) return -1;
    beginState->outList[outCount] = edge;
    beginState->outCount += 1;

    node->edgesCount += 1;
    return nextIdx;
}

static void atree_delete(R3D_AnimationTreeNode anode)
{
    switch(anode.base->type)
    {
    case R3D_ANIMTREE_ANIM:
    case R3D_ANIMTREE_BLEND2:
    case R3D_ANIMTREE_ADD2:
    case R3D_ANIMTREE_STM_X:
        return;
    case R3D_ANIMTREE_SWITCH:
        r3d_free(anode.swch->inList);
        r3d_free(anode.swch->inWeights);
        return;
    case R3D_ANIMTREE_STM:
        for (int i = 0; i < anode.stm->statesCount; i++)
        {
            if (anode.stm->stateList[i].outList)
            {
                r3d_free(anode.stm->stateList[i].outList);
            }
        }
        r3d_free(anode.stm->nodeList);
        r3d_free(anode.stm->edgeList);
        r3d_free(anode.stm->stateList);
        r3d_free(anode.stm->visitList);
        if (anode.stm->path.edges) r3d_free(anode.stm->path.edges);
        if (anode.stm->path.open) r3d_free(anode.stm->path.open);
        if (anode.stm->path.next) r3d_free(anode.stm->path.next);
        if (anode.stm->path.mark) r3d_free(anode.stm->path.mark);
        return;
    default:
        R3D_TRACELOG(LOG_WARNING, "Failed to delete node: invalid type %d", anode.base->type);
        break;
    }
}

static void atree_travel(r3d_animtree_stm_t* node, R3D_AnimationStmIndex targetIdx)
{
    if (node->activeIdx == targetIdx) return;

    bool found = stm_find_path(node, targetIdx);
    if (!found)
    {
        r3d_stmstate_t* state = &node->stateList[targetIdx];
        R3D_AnimationTreeNode anode = node->nodeList[targetIdx];
        state->activeIn = NULL;
        node->activeIdx = targetIdx;
        node->path.len = 0;
        anode_reset(anode);
    }
}

// ========================================
// PUBLIC API
// ========================================

R3D_AnimationTree R3D_LoadAnimationTree(R3D_AnimationPlayer player, int maxSize)
{
    return R3D_LoadAnimationTreePro(player, maxSize, -1, NULL, NULL);
}

R3D_AnimationTree R3D_LoadAnimationTreeEx(R3D_AnimationPlayer player, int maxSize, int rootBone)
{
    return R3D_LoadAnimationTreePro(player, maxSize, rootBone, NULL, NULL);
}

R3D_AnimationTree R3D_LoadAnimationTreePro(R3D_AnimationPlayer player, int maxSize, int rootBone,
                                           R3D_AnimationTreeCallback updateCallback,
                                           void* updateUserData)
{
    R3D_AnimationTree tree = {0};
    tree.player = player;
    tree.nodePool = r3d_zalloc(maxSize * sizeof(*tree.nodePool));
    tree.nodePoolMaxSize = maxSize;
    tree.rootBone = rootBone;
    tree.updateCallback = updateCallback;
    tree.updateUserData = updateUserData;
    return tree;
}

void R3D_UnloadAnimationTree(R3D_AnimationTree tree)
{
    int poolSize = tree.nodePoolSize;
    for (int i = 0; i < poolSize; i++)
    {
        R3D_AnimationTreeNode node = tree.nodePool[i];
        atree_delete(node);
        r3d_free(node.base);
    }
    r3d_free(tree.nodePool);
}

void R3D_UpdateAnimationTree(R3D_AnimationTree* tree, float dt)
{
    R3D_UpdateAnimationTreeEx(tree, dt, NULL, NULL);
}

bool R3D_AdvanceAnimationTree(R3D_AnimationTree* tree, float dt)
{
    if (!tree || !tree->rootNode || !tree->rootNode->base) return false;
    if (!isfinite(dt) || dt < 0.0f) return false;

    return anode_update(tree, *tree->rootNode, dt, NULL);
}

bool R3D_ComputeAnimationTreeLocalPose(R3D_AnimationTree* tree)
{
    return R3D_ComputeAnimationTreeLocalPoseEx(tree, NULL, NULL);
}

bool R3D_ComputeAnimationTreeLocalPoseEx(R3D_AnimationTree* tree, Transform* rootMotion, Transform* rootDistance)
{
    if (!tree || !tree->rootNode || !tree->rootNode->base) return false;

    R3D_AnimationPlayer* player = &tree->player;
    const int boneCount = player->skeleton.boneCount;

    if (boneCount <= 0 || !player->localPose || !player->skeleton.localBind) return false;

    for (int boneIdx = 0; boneIdx < boneCount; boneIdx++)
    {
        const bool isRootBone = is_root_bone(tree, boneIdx);

        rminfo_t rmInfo = {0};
        Transform out = {0};

        if (!anode_eval(tree, *tree->rootNode, boneIdx, &out, isRootBone ? &rmInfo : NULL))
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to evaluate animation tree local pose");
            return false;
        }

        if (isRootBone)
        {
            if (rootMotion) *rootMotion = rmInfo.motion;
            if (rootDistance) *rootDistance = rmInfo.distance;

            out = r3d_anim_transform_subtr(out, rmInfo.distance);
        }

        if (tree->updateCallback)
        {
            tree->updateCallback(player, boneIdx, &out, tree->updateUserData);
        }

        player->localPose[boneIdx] = r3d_matrix_srt_quat(out.scale, out.rotation, out.translation);
    }

    return true;
}

bool R3D_ComputeAnimationTreeModelPose(R3D_AnimationTree* tree)
{
    if (!tree) return false;

    return R3D_RecomputeAnimationModelPose(&tree->player);
}

void R3D_UpdateAnimationTreeEx(R3D_AnimationTree* tree, float dt, Transform* rootMotion, Transform* rootDistance)
{
    if (!tree || !isfinite(dt) || dt < 0.0f) return;

    // Morph-only animation trees have no skeletal pose.
    // Their animation states still need to advance.
    if (tree->player.skeleton.boneCount <= 0)
    {
        if (!R3D_AdvanceAnimationTree(tree, dt))
        {
            R3D_TRACELOG(
                LOG_ERROR,
                "Morph-only animation tree update failed"
            );
        }

        return;
    }

    if (R3D_AdvanceAnimationTree(tree, dt) &&
        R3D_ComputeAnimationTreeLocalPoseEx(tree, rootMotion, rootDistance) &&
        R3D_ComputeAnimationTreeModelPose(tree))
    {
        R3D_UploadAnimationPose(&tree->player);
        return;
    }

    R3D_TRACELOG(LOG_ERROR, "Animation tree failed");

    R3D_AnimationPlayer* player = &tree->player;
    const int boneCount = player->skeleton.boneCount;

    if (boneCount <= 0 || !player->localPose || !player->modelPose ||
        !player->skeleton.localBind || !player->skeleton.modelBind)
    {
        return;
    }

    memcpy(player->localPose, player->skeleton.localBind, boneCount * sizeof(Matrix));
    memcpy(player->modelPose, player->skeleton.modelBind, boneCount * sizeof(Matrix));

    R3D_UploadAnimationPose(player);
}

void R3D_AddRootAnimationNode(R3D_AnimationTree* tree, R3D_AnimationTreeNode* node)
{
    tree->rootNode = node;
}

bool R3D_AddAnimationNode(R3D_AnimationTreeNode* parent, R3D_AnimationTreeNode* node, int inputIndex)
{
    switch(parent->base->type)
    {
    case R3D_ANIMTREE_ANIM:
    case R3D_ANIMTREE_STM:
    case R3D_ANIMTREE_STM_X:  return false;
    case R3D_ANIMTREE_BLEND2: return atree_blend2_add(parent->bln2, *node, inputIndex);
    case R3D_ANIMTREE_ADD2:   return atree_add2_add(parent->add2, *node, inputIndex);
    case R3D_ANIMTREE_SWITCH: return atree_switch_add(parent->swch, *node, inputIndex);
    default:
        R3D_TRACELOG(LOG_WARNING, "Failed to add animation node: invalid parent type %d", parent->base->type);
        break;
    }
    return false;
}

R3D_AnimationTreeNode* R3D_GetAnimationNode(R3D_AnimationTreeNode* parent, int inputIndex)
{
    if (!parent || !parent->base || inputIndex < 0) return NULL;

    switch (parent->base->type)
    {
        case R3D_ANIMTREE_BLEND2:
            if (inputIndex == 0 && parent->bln2->inMain.base) return &parent->bln2->inMain;
            if (inputIndex == 1 && parent->bln2->inBlend.base) return &parent->bln2->inBlend;
            break;

        case R3D_ANIMTREE_ADD2:
            if (inputIndex == 0 && parent->add2->inMain.base) return &parent->add2->inMain;
            if (inputIndex == 1 && parent->add2->inAdd.base) return &parent->add2->inAdd;
            break;

        case R3D_ANIMTREE_SWITCH:
            if (inputIndex < parent->swch->inCount && parent->swch->inList[inputIndex].base)
            {
                return &parent->swch->inList[inputIndex];
            }
            break;

        case R3D_ANIMTREE_STM:
            if (inputIndex < parent->stm->statesCount && parent->stm->nodeList[inputIndex].base)
            {
                return &parent->stm->nodeList[inputIndex];
            }
            break;

        case R3D_ANIMTREE_STM_X:
            if (inputIndex == 0 && parent->stmx->nested.base) return &parent->stmx->nested;
            break;

        default:
            break;
    }

    return NULL;
}

R3D_AnimationTreeNode* R3D_GetActiveAnimationNode(R3D_AnimationTreeNode* node)
{
    while (node && node->base)
    {
        switch (node->base->type)
        {
            case R3D_ANIMTREE_ANIM:
                return node;

            case R3D_ANIMTREE_SWITCH:
                node = R3D_GetAnimationNode(node, node->swch->params.activeInput);
                break;

            case R3D_ANIMTREE_STM:
                node = R3D_GetAnimationNode(node, node->stm->activeIdx);
                break;

            case R3D_ANIMTREE_STM_X:
                node = R3D_GetAnimationNode(node, 0);
                break;

            default:
                return NULL;
        }
    }

    return NULL;
}

R3D_AnimationTreeNode* R3D_CreateAnimationNode(R3D_AnimationTree* tree, R3D_AnimationNodeParams params)
{
    return atree_anim_create(tree, params, false);
}

R3D_AnimationTreeNode* R3D_CreateAnimationNodeEx(R3D_AnimationTree* tree, R3D_AnimationNodeParams params, bool setTime)
{
    return atree_anim_create(tree, params, setTime);
}

R3D_AnimationTreeNode* R3D_CreateBlend2Node(R3D_AnimationTree* tree, R3D_Blend2NodeParams params)
{
    return atree_blend2_create(tree, params);
}

R3D_AnimationTreeNode* R3D_CreateAdd2Node(R3D_AnimationTree* tree, R3D_Add2NodeParams params)
{
    return atree_add2_create(tree, params);
}

R3D_AnimationTreeNode* R3D_CreateSwitchNode(R3D_AnimationTree* tree, int inputCount, R3D_SwitchNodeParams params)
{
    return atree_switch_create(tree, inputCount, params);
}

R3D_AnimationTreeNode* R3D_CreateStmNode(R3D_AnimationTree* tree, int statesCount, int edgesCount)
{
    return atree_stm_create(tree, statesCount, edgesCount, false);
}

R3D_AnimationTreeNode* R3D_CreateStmNodeEx(R3D_AnimationTree* tree, int statesCount, int edgesCount, bool enableTravel)
{
    return atree_stm_create(tree, statesCount, edgesCount, enableTravel);
}

R3D_AnimationTreeNode* R3D_CreateStmXNode(R3D_AnimationTree* tree, R3D_AnimationTreeNode* nestedNode)
{
    return atree_stm_x_create(tree, *nestedNode);
}

R3D_AnimationStmIndex R3D_CreateStmNodeState(R3D_AnimationTreeNode* stmNode, R3D_AnimationTreeNode* stateNode, int outEdgesCount)
{
    return atree_state_create(stmNode->stm, *stateNode, outEdgesCount);
}

R3D_AnimationStmIndex R3D_CreateStmNodeEdge(R3D_AnimationTreeNode* stmNode, R3D_AnimationStmIndex beginStateIndex,
                                            R3D_AnimationStmIndex endStateIndex, R3D_StmEdgeParams params)
{
    return atree_edge_create(stmNode->stm, beginStateIndex, endStateIndex, params);
}

void R3D_SetAnimationNodeParams(R3D_AnimationTreeNode* node, R3D_AnimationNodeParams params)
{
    node->anim->params = params;
}

R3D_AnimationNodeParams R3D_GetAnimationNodeParams(R3D_AnimationTreeNode* node)
{
    return node->anim->params;
}

float R3D_GetAnimationNodeTimeSeconds(const R3D_AnimationTreeNode* node)
{
    if (!node || !node->base || node->base->type != R3D_ANIMTREE_ANIM) return 0.0f;

    return node->anim->params.state.currentTime;
}

float R3D_GetAnimationNodeDurationSeconds(const R3D_AnimationTreeNode* node)
{
    if (!node || !node->base || node->base->type != R3D_ANIMTREE_ANIM) return 0.0f;

    const R3D_Animation* anim = node->anim->animation;
    if (!anim) return 0.0f;

    if (!isfinite(anim->duration) || !isfinite(anim->ticksPerSecond)) return 0.0f;
    if (anim->duration <= 0.0f || anim->ticksPerSecond <= 0.0f) return 0.0f;

    return anim->duration / anim->ticksPerSecond;
}

float R3D_GetAnimationNodeNormalizedTime(const R3D_AnimationTreeNode* node)
{
    float duration = R3D_GetAnimationNodeDurationSeconds(node);
    if (duration <= 0.0f) return 0.0f;

    float time = R3D_GetAnimationNodeTimeSeconds(node);
    return Clamp(time / duration, 0.0f, 1.0f);
}

void R3D_SetAnimationNodeTimeSeconds(R3D_AnimationTree* tree, R3D_AnimationTreeNode* node, float timeSeconds)
{
    if (!tree || !node || !node->base || node->base->type != R3D_ANIMTREE_ANIM) return;
    if (!isfinite(timeSeconds)) return;

    r3d_animtree_anim_t* animNode = node->anim;
    const R3D_Animation* anim = animNode->animation;

    float duration = R3D_GetAnimationNodeDurationSeconds(node);
    float time = Clamp(timeSeconds, 0.0f, duration);

    animNode->params.state.currentTime = time;
    animNode->root.loops = -1;

    int rootBone = tree->rootBone;
    if (rootBone >= 0 && rootBone < tree->player.skeleton.boneCount)
    {
        const R3D_AnimationChannel* channel = r3d_anim_channel_find(anim, rootBone);

        if (channel)
        {
            animNode->root.last = r3d_anim_channel_lerp(channel, time * anim->ticksPerSecond, &tree->player.skeleton.localBind[rootBone], NULL, NULL);
        }
    }
}

void R3D_SetBlend2NodeParams(R3D_AnimationTreeNode* node, R3D_Blend2NodeParams params)
{
    node->bln2->params = params;
}

R3D_Blend2NodeParams R3D_GetBlend2NodeParams(R3D_AnimationTreeNode* node)
{
    return node->bln2->params;
}

void R3D_SetAdd2NodeParams(R3D_AnimationTreeNode* node, R3D_Add2NodeParams params)
{
    node->add2->params = params;
}

R3D_Add2NodeParams R3D_GetAdd2NodeParams(R3D_AnimationTreeNode* node)
{
    return node->add2->params;
}

void R3D_SetSwitchNodeParams(R3D_AnimationTreeNode* node, R3D_SwitchNodeParams params)
{
    node->swch->params = params;
}

R3D_SwitchNodeParams R3D_GetSwitchNodeParams(R3D_AnimationTreeNode* node)
{
    return node->swch->params;
}

void R3D_SetStmNodeEdgeParams(R3D_AnimationTreeNode* node, R3D_AnimationStmIndex edgeIndex, R3D_StmEdgeParams params)
{
    node->stm->edgeList[edgeIndex].params = params;
}

R3D_StmEdgeParams R3D_GetStmNodeEdgeParams(R3D_AnimationTreeNode* node, R3D_AnimationStmIndex edgeIndex)
{
    return node->stm->edgeList[edgeIndex].params;
}

R3D_AnimationStmIndex R3D_GetStmStateActiveIndex(R3D_AnimationTreeNode* node)
{
    return node->stm->activeIdx;
}

void R3D_TravelToStmState(R3D_AnimationTreeNode* node, R3D_AnimationStmIndex targetStateIndex)
{
    atree_travel(node->stm, targetStateIndex);
}

R3D_BoneMask R3D_ComputeBoneMask(const R3D_Skeleton* skeleton, const char* boneNames[], int boneNameCount)
{
    const R3D_BoneInfo* bones = skeleton->bones;
    const int bCount = skeleton->boneCount;
    R3D_BoneMask bMask = {.boneCount = bCount};

    if ((size_t)bCount > sizeof(bMask.mask) * 8)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to compute bone mask: max bone count exceeded (%d)", sizeof(bMask.mask) * 8);
        return (R3D_BoneMask){0};
    }

    int maskBits = sizeof(bMask.mask[0]) * 8;
    for (int i = 0; i < boneNameCount; i++)
    {
        const char* name = boneNames[i];

        bool found = false;
        for (int bIdx = 0; bIdx < bCount; bIdx++)
        {
            if (!strncmp(name, bones[bIdx].name, sizeof(bones[bIdx].name)))
            {
                int part = bIdx / maskBits;
                int bit = bIdx % maskBits;
                bMask.mask[part] |= UINT32_C(1) << bit;
                found = true;
                break;
            }
        }

        if (!found)
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to compute bone mask: bone \"%s\" not found", name);
            return (R3D_BoneMask){0};
        }
    }

    return bMask;
}

R3D_BoneMask R3D_ComputeAnimationBoneMask(const R3D_Animation* animation)
{
    R3D_BoneMask boneMask = {0};

    if (!animation)
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to compute animation bone mask: animation is NULL");
        return boneMask;
    }

    const int boneCount = animation->boneCount;

    if (boneCount < 0 || boneCount > 256 || animation->channelCount < 0 ||
        (animation->channelCount > 0 && !animation->channels))
    {
        R3D_TRACELOG(LOG_WARNING, "Failed to compute animation bone mask: invalid animation data");
        return boneMask;
    }

    boneMask.boneCount = boneCount;

    for (int i = 0; i < animation->channelCount; i++)
    {
        const R3D_AnimationChannel* channel = &animation->channels[i];

        // Ignore channels without animation keys.
        if (channel->translation.count <= 0 &&
            channel->rotation.count <= 0 &&
            channel->scale.count <= 0)
        {
            continue;
        }

        const int boneIdx = channel->boneIndex;

        if (boneIdx < 0 || boneIdx >= boneCount)
        {
            R3D_TRACELOG(LOG_WARNING, "Failed to compute animation bone mask: invalid bone index %d", boneIdx);
            return (R3D_BoneMask){0};
        }

        boneMask.mask[boneIdx / 32] |= UINT32_C(1) << (boneIdx % 32);
    }

    return boneMask;
}