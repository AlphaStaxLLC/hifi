//
//  VoxelNode.cpp
//  hifi
//
//  Created by Stephen Birarda on 3/13/13.
//  Copyright (c) 2013 HighFidelity, Inc. All rights reserved.
//

#include <cmath>
#include <cstring>
#include <stdio.h>

#include <QtCore/QDebug>

#include <NodeList.h>

#include "AABox.h"
#include "OctalCode.h"
#include "SharedUtil.h"
#include "VoxelConstants.h"
#include "VoxelNode.h"
#include "VoxelTree.h"

uint64_t VoxelNode::_voxelMemoryUsage = 0;
uint64_t VoxelNode::_octcodeMemoryUsage = 0;
uint64_t VoxelNode::_voxelNodeCount = 0;
uint64_t VoxelNode::_voxelNodeLeafCount = 0;

VoxelNode::VoxelNode() {
    unsigned char* rootCode = new unsigned char[1];
    *rootCode = 0;
    init(rootCode);

    _voxelNodeCount++;
    _voxelNodeLeafCount++; // all nodes start as leaf nodes
}

VoxelNode::VoxelNode(unsigned char * octalCode) {
    init(octalCode);
    _voxelNodeCount++;
    _voxelNodeLeafCount++; // all nodes start as leaf nodes
}

void VoxelNode::init(unsigned char * octalCode) {
    int octalCodeLength = bytesRequiredForCodeLength(numberOfThreeBitSectionsInCode(octalCode));
    if (octalCodeLength > sizeof(_octalCode)) {
        _octalCode._octalCodePointer = octalCode;
        _octcodePointer = true;
        _octcodeMemoryUsage += octalCodeLength;
    } else {
        _octcodePointer = false;
        memcpy(_octalCode._octalCodeBuffer, octalCode, octalCodeLength);
        delete[] octalCode;
    }
    
#ifndef NO_FALSE_COLOR // !NO_FALSE_COLOR means, does have false color
    _falseColored = false; // assume true color
    _currentColor[0] = _currentColor[1] = _currentColor[2] = _currentColor[3] = 0;
#endif
    _trueColor[0] = _trueColor[1] = _trueColor[2] = _trueColor[3] = 0;
    _density = 0.0f;
    
    // default pointers to child nodes to NULL
    for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
        _children[i] = NULL;
    }
    _childCount = 0;
    
    _unknownBufferIndex = true;
    setBufferIndex(GLBUFFER_INDEX_UNKNOWN);
    setVoxelSystem(NULL);
    _isDirty = true;
    _shouldRender = false;
    _sourceID = UNKNOWN_NODE_ID;
    calculateAABox();
    markWithChangedTime();

    _voxelMemoryUsage += sizeof(VoxelNode);
}

VoxelNode::~VoxelNode() {
    notifyDeleteHooks();

    _voxelMemoryUsage -= sizeof(VoxelNode);

    _voxelNodeCount--;
    if (isLeaf()) {
        _voxelNodeLeafCount--;
    }

    if (_octcodePointer) {
        _octcodeMemoryUsage -= bytesRequiredForCodeLength(numberOfThreeBitSectionsInCode(getOctalCode()));
        delete[] _octalCode._octalCodePointer;
    }
    
    // delete all of this node's children
    for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
        if (_children[i]) {
            delete _children[i];
        }
    }
}

void VoxelNode::markWithChangedTime() { 
    _lastChanged = usecTimestampNow(); 
    notifyUpdateHooks(); // if the node has changed, notify our hooks
}

// This method is called by VoxelTree when the subtree below this node
// is known to have changed. It's intended to be used as a place to do
// bookkeeping that a node may need to do when the subtree below it has
// changed. However, you should hopefully make your bookkeeping relatively
// localized, because this method will get called for every node in an
// recursive unwinding case like delete or add voxel
void VoxelNode::handleSubtreeChanged(VoxelTree* myTree) {
    // here's a good place to do color re-averaging...
    if (myTree->getShouldReaverage()) {
        setColorFromAverageOfChildren();
    }
    
    markWithChangedTime();
}

uint8_t VoxelNode::_nextIndex = 0;
std::map<VoxelSystem*, uint8_t> VoxelNode::_mapVoxelSystemPointersToIndex;
std::map<uint8_t, VoxelSystem*> VoxelNode::_mapIndexToVoxelSystemPointers;

VoxelSystem* VoxelNode::getVoxelSystem() const { 
    if (_mapIndexToVoxelSystemPointers.end() != _mapIndexToVoxelSystemPointers.find(_voxelSystemIndex)) {
        return _mapIndexToVoxelSystemPointers[_voxelSystemIndex]; 
    }
    return NULL;
}

void VoxelNode::setVoxelSystem(VoxelSystem* voxelSystem) {
    uint8_t index;
    if (_mapVoxelSystemPointersToIndex.end() != _mapVoxelSystemPointersToIndex.find(voxelSystem)) {
        index = _mapVoxelSystemPointersToIndex[voxelSystem];
    } else {
        index = _nextIndex;
        _nextIndex++;
        _mapVoxelSystemPointersToIndex[voxelSystem] = index;
        _mapIndexToVoxelSystemPointers[index] = voxelSystem;
    }
    _voxelSystemIndex = index;
}


void VoxelNode::setShouldRender(bool shouldRender) {
    // if shouldRender is changing, then consider ourselves dirty
    if (shouldRender != _shouldRender) {
        _shouldRender = shouldRender;
        _isDirty = true;
        markWithChangedTime();
    }
}

void VoxelNode::calculateAABox() {
    glm::vec3 corner;
    
    // copy corner into box
    copyFirstVertexForCode(getOctalCode(),(float*)&corner);
    
    // this tells you the "size" of the voxel
    float voxelScale = 1 / powf(2, numberOfThreeBitSectionsInCode(getOctalCode()));
    _box.setBox(corner,voxelScale);
}

void VoxelNode::deleteChildAtIndex(int childIndex) {
    if (_children[childIndex]) {
        delete _children[childIndex];
        _children[childIndex] = NULL;
        _isDirty = true;
        _childCount--;
        markWithChangedTime();
        
        // after deleting the child, check to see if we're a leaf
        if (isLeaf()) {
            _voxelNodeLeafCount++;
        }
    }
}

// does not delete the node!
VoxelNode* VoxelNode::removeChildAtIndex(int childIndex) {
    VoxelNode* returnedChild = _children[childIndex];
    if (_children[childIndex]) {
        _children[childIndex] = NULL;
        _isDirty = true;
        _childCount--;
        markWithChangedTime();
        
        // after removing the child, check to see if we're a leaf
        if (isLeaf()) {
            _voxelNodeLeafCount++;
        }
    }
    return returnedChild;
}

VoxelNode* VoxelNode::addChildAtIndex(int childIndex) {
    if (!_children[childIndex]) {
        // before adding a child, see if we're currently a leaf 
        if (isLeaf()) {
            _voxelNodeLeafCount--;
        }
    
        _children[childIndex] = new VoxelNode(childOctalCode(getOctalCode(), childIndex));
        _children[childIndex]->setVoxelSystem(getVoxelSystem()); // our child is always part of our voxel system NULL ok
        _isDirty = true;
        _childCount++;
        markWithChangedTime();
    }
    return _children[childIndex];
}

// handles staging or deletion of all deep children
void VoxelNode::safeDeepDeleteChildAtIndex(int childIndex) {
    VoxelNode* childToDelete = getChildAtIndex(childIndex);
    if (childToDelete) {
        // If the child is not a leaf, then call ourselves recursively on all the children
        if (!childToDelete->isLeaf()) {
            // delete all it's children
            for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
                childToDelete->safeDeepDeleteChildAtIndex(i);
            }
        }
        deleteChildAtIndex(childIndex);
        _isDirty = true;
        markWithChangedTime();
    }
}

// will average the child colors...
void VoxelNode::setColorFromAverageOfChildren() {
    int colorArray[4] = {0,0,0,0};
    float density = 0.0f;
    for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
        if (_children[i] && _children[i]->isColored()) {
            for (int j = 0; j < 3; j++) {
                colorArray[j] += _children[i]->getTrueColor()[j]; // color averaging should always be based on true colors
            }
            colorArray[3]++;
        }
        if (_children[i]) {
            density += _children[i]->getDensity();
        }
    }
    density /= (float) NUMBER_OF_CHILDREN;    
    //
    //  The VISIBLE_ABOVE_DENSITY sets the density of matter above which an averaged color voxel will
    //  be set.  It is an important physical constant in our universe.  A number below 0.5 will cause
    //  things to get 'fatter' at a distance, because upward averaging will make larger voxels out of
    //  less data, which is (probably) going to be preferable because it gives a sense that there is
    //  something out there to go investigate.   A number above 0.5 would cause the world to become
    //  more 'empty' at a distance.  Exactly 0.5 would match the physical world, at least for materials
    //  that are not shiny and have equivalent ambient reflectance.  
    //
    const float VISIBLE_ABOVE_DENSITY = 0.10f;        
    nodeColor newColor = { 0, 0, 0, 0};
    if (density > VISIBLE_ABOVE_DENSITY) {
        // The density of material in the space of the voxel sets whether it is actually colored
        for (int c = 0; c < 3; c++) {
            // set the average color value
            newColor[c] = colorArray[c] / colorArray[3];
        }
        // set the alpha to 1 to indicate that this isn't transparent
        newColor[3] = 1;
    }
    //  Set the color from the average of the child colors, and update the density 
    setColor(newColor);
    setDensity(density);
}

// Note: !NO_FALSE_COLOR implementations of setFalseColor(), setFalseColored(), and setColor() here.
//       the actual NO_FALSE_COLOR version are inline in the VoxelNode.h
#ifndef NO_FALSE_COLOR // !NO_FALSE_COLOR means, does have false color
void VoxelNode::setFalseColor(colorPart red, colorPart green, colorPart blue) {
    if (_falseColored != true || _currentColor[0] != red || _currentColor[1] != green || _currentColor[2] != blue) {
        _falseColored=true;
        _currentColor[0] = red;
        _currentColor[1] = green;
        _currentColor[2] = blue;
        _currentColor[3] = 1; // XXXBHG - False colors are always considered set
        _isDirty = true;
        markWithChangedTime();
    }
}

void VoxelNode::setFalseColored(bool isFalseColored) {
    if (_falseColored != isFalseColored) {
        // if we were false colored, and are no longer false colored, then swap back
        if (_falseColored && !isFalseColored) {
            memcpy(&_currentColor,&_trueColor,sizeof(nodeColor));
        }
        _falseColored = isFalseColored; 
        _isDirty = true;
        _density = 1.0f;       //   If color set, assume leaf, re-averaging will update density if needed.
        markWithChangedTime();
    }
};


void VoxelNode::setColor(const nodeColor& color) {
    if (_trueColor[0] != color[0] || _trueColor[1] != color[1] || _trueColor[2] != color[2]) {
        memcpy(&_trueColor,&color,sizeof(nodeColor));
        if (!_falseColored) {
            memcpy(&_currentColor,&color,sizeof(nodeColor));
        }
        _isDirty = true;
        _density = 1.0f;       //   If color set, assume leaf, re-averaging will update density if needed.
        markWithChangedTime();
    }
}
#endif

// will detect if children are leaves AND the same color
// and in that case will delete the children and make this node
// a leaf, returns TRUE if all the leaves are collapsed into a 
// single node
bool VoxelNode::collapseIdenticalLeaves() {
    // scan children, verify that they are ALL present and accounted for
    bool allChildrenMatch = true; // assume the best (ottimista)
    int red,green,blue;
    for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
        // if no child, child isn't a leaf, or child doesn't have a color
        if (!_children[i] || !_children[i]->isLeaf() || !_children[i]->isColored()) {
            allChildrenMatch=false;
            //qDebug("SADNESS child missing or not colored! i=%d\n",i);
            break;
        } else {
            if (i==0) {
                red   = _children[i]->getColor()[0];
                green = _children[i]->getColor()[1];
                blue  = _children[i]->getColor()[2];
            } else if (red != _children[i]->getColor()[0] || 
                    green != _children[i]->getColor()[1] || blue != _children[i]->getColor()[2]) {
                allChildrenMatch=false;
                break;
            }
        }
    }
    
    
    if (allChildrenMatch) {
        //qDebug("allChildrenMatch: pruning tree\n");
        for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
            delete _children[i]; // delete all the child nodes
            _children[i]=NULL; // set it to NULL
        }
        _childCount = 0;
        nodeColor collapsedColor;
        collapsedColor[0]=red;        
        collapsedColor[1]=green;        
        collapsedColor[2]=blue;        
        collapsedColor[3]=1;    // color is set
        setColor(collapsedColor);
    }
    return allChildrenMatch;
}

void VoxelNode::setRandomColor(int minimumBrightness) {
    nodeColor newColor;
    for (int c = 0; c < 3; c++) {
        newColor[c] = randomColorValue(minimumBrightness);
    }
    
    newColor[3] = 1;
    setColor(newColor);
}

void VoxelNode::printDebugDetails(const char* label) const {
    unsigned char childBits = 0;
    for (int i = 0; i < NUMBER_OF_CHILDREN; i++) {
        if (_children[i]) {
            setAtBit(childBits,i);            
        }
    }

    qDebug("%s - Voxel at corner=(%f,%f,%f) size=%f\n isLeaf=%s isColored=%s (%d,%d,%d,%d) isDirty=%s shouldRender=%s\n children=", label,
        _box.getCorner().x, _box.getCorner().y, _box.getCorner().z, _box.getScale(),
        debug::valueOf(isLeaf()), debug::valueOf(isColored()), getColor()[0], getColor()[1], getColor()[2], getColor()[3],
        debug::valueOf(isDirty()), debug::valueOf(getShouldRender()));
        
    outputBits(childBits, false);
    qDebug("\n octalCode=");
    printOctalCode(getOctalCode());
}

float VoxelNode::getEnclosingRadius() const {
    return getScale() * sqrtf(3.0f) / 2.0f;
}

bool VoxelNode::isInView(const ViewFrustum& viewFrustum) const {
    AABox box = _box; // use temporary box so we can scale it
    box.scale(TREE_SCALE);
    bool inView = (ViewFrustum::OUTSIDE != viewFrustum.boxInFrustum(box));
    return inView;
}

ViewFrustum::location VoxelNode::inFrustum(const ViewFrustum& viewFrustum) const {
    AABox box = _box; // use temporary box so we can scale it
    box.scale(TREE_SCALE);
    return viewFrustum.boxInFrustum(box);
}

// There are two types of nodes for which we want to "render"
// 1) Leaves that are in the LOD
// 2) Non-leaves are more complicated though... usually you don't want to render them, but if their children
//    wouldn't be rendered, then you do want to render them. But sometimes they have some children that ARE 
//    in the LOD, and others that are not. In this case we want to render the parent, and none of the children.
//
//    Since, if we know the camera position and orientation, we can know which of the corners is the "furthest" 
//    corner. We can use we can use this corner as our "voxel position" to do our distance calculations off of.
//    By doing this, we don't need to test each child voxel's position vs the LOD boundary
bool VoxelNode::calculateShouldRender(const ViewFrustum* viewFrustum, int boundaryLevelAdjust) const {
    bool shouldRender = false;
    if (isColored()) {
        float furthestDistance = furthestDistanceToCamera(*viewFrustum);
        float boundary         = boundaryDistanceForRenderLevel(getLevel() + boundaryLevelAdjust);
        float childBoundary    = boundaryDistanceForRenderLevel(getLevel() + 1 + boundaryLevelAdjust);
        bool  inBoundary       = (furthestDistance <= boundary);
        bool  inChildBoundary  = (furthestDistance <= childBoundary);
        shouldRender = (isLeaf() && inChildBoundary) || (inBoundary && !inChildBoundary);
    }
    return shouldRender;
}

// Calculates the distance to the furthest point of the voxel to the camera
float VoxelNode::furthestDistanceToCamera(const ViewFrustum& viewFrustum) const {
    AABox box = getAABox();
    box.scale(TREE_SCALE);
    glm::vec3 furthestPoint = viewFrustum.getFurthestPointFromCamera(box);
    glm::vec3 temp = viewFrustum.getPosition() - furthestPoint;
    float distanceToVoxelCenter = sqrtf(glm::dot(temp, temp));
    return distanceToVoxelCenter;
}

float VoxelNode::distanceToCamera(const ViewFrustum& viewFrustum) const {
    glm::vec3 center = _box.calcCenter() * (float)TREE_SCALE;
    glm::vec3 temp = viewFrustum.getPosition() - center;
    float distanceToVoxelCenter = sqrtf(glm::dot(temp, temp));
    return distanceToVoxelCenter;
}

float VoxelNode::distanceSquareToPoint(const glm::vec3& point) const {
    glm::vec3 temp = point - _box.calcCenter();
    float distanceSquare = glm::dot(temp, temp);
    return distanceSquare;
}

float VoxelNode::distanceToPoint(const glm::vec3& point) const {
    glm::vec3 temp = point - _box.calcCenter();
    float distance = sqrtf(glm::dot(temp, temp));
    return distance;
}

std::vector<VoxelNodeDeleteHook*> VoxelNode::_deleteHooks;

void VoxelNode::addDeleteHook(VoxelNodeDeleteHook* hook) {
    _deleteHooks.push_back(hook);
}

void VoxelNode::removeDeleteHook(VoxelNodeDeleteHook* hook) {
    for (int i = 0; i < _deleteHooks.size(); i++) {
        if (_deleteHooks[i] == hook) {
            _deleteHooks.erase(_deleteHooks.begin() + i);
            return;
        }
    }
}

void VoxelNode::notifyDeleteHooks() {
    for (int i = 0; i < _deleteHooks.size(); i++) {
        _deleteHooks[i]->voxelDeleted(this);
    }
}

std::vector<VoxelNodeUpdateHook*> VoxelNode::_updateHooks;

void VoxelNode::addUpdateHook(VoxelNodeUpdateHook* hook) {
    _updateHooks.push_back(hook);
}

void VoxelNode::removeUpdateHook(VoxelNodeUpdateHook* hook) {
    for (int i = 0; i < _updateHooks.size(); i++) {
        if (_updateHooks[i] == hook) {
            _updateHooks.erase(_updateHooks.begin() + i);
            return;
        }
    }
}

void VoxelNode::notifyUpdateHooks() {
    for (int i = 0; i < _updateHooks.size(); i++) {
        _updateHooks[i]->voxelUpdated(this);
    }
}