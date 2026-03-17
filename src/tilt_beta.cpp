

#include <stdint.h>

#include "AppManager.h"
#include "TiltGridTest.h"
#include "native.h"

namespace
{

constexpr uint32_t kPlayerPositionMessageType = 1u;

constexpr int kScreenSize = 240;
constexpr int kTilesPerScreen = 2;
constexpr int kCellSize = kScreenSize / kTilesPerScreen;
constexpr int kCellCenterOffset = kCellSize / 2;
constexpr int kGridLineThickness = 4;
constexpr int kCircleRadius = 28;

constexpr uint32_t kBackgroundColor = 0xFF111111u;
constexpr uint32_t kGridColor = 0xFF4E5A66u;
constexpr uint32_t kGridBorderColor = 0xFF82909Du;
constexpr uint32_t kPlayerShadowColor = 0xFF203040u;
constexpr uint32_t kPlayerFillColor = 0xFFF5C542u;
constexpr uint32_t kPlayerHighlightColor = 0xFFFFED9Eu;

constexpr int32_t kTiltThreshold = 20;
constexpr float kAccelSmoothAlpha = 0.3f;
constexpr uint32_t kMoveCooldownMS = 500u;
constexpr uint32_t kMoveAnimMS = 220u;

int iabs32(int value)
{
  return value < 0 ? -value : value;
}

float clamp01(float value)
{
  if (value < 0.0f)
  {
    return 0.0f;
  }
  if (value > 1.0f)
  {
    return 1.0f;
  }
  return value;
}

} // namespace

void TiltGridTest::syncOwnerFromCurrentScreen()
{
  this->playerModule = static_cast<uint8_t>(this->currentScreenID / 3);
  this->playerLocalScreen = static_cast<uint8_t>(this->currentScreenID % 3);
}

void TiltGridTest::snapRenderToGrid()
{
  this->renderX = static_cast<float>(this->gridX);
  this->renderY = static_cast<float>(this->gridY);
}

void TiltGridTest::broadcastPlayerPosition() const
{
  PlayerPositionPacket pkt{};
  pkt.moduleID = this->playerModule;
  pkt.screenIndex = this->playerLocalScreen;
  pkt.gridX = static_cast<uint8_t>(this->gridX);
  pkt.gridY = static_cast<uint8_t>(this->gridY);

  Cubios::sendPacket(kPlayerPositionMessageType,
                     reinterpret_cast<uint8_t *>(&pkt), sizeof(pkt));
}

int TiltGridTest::getNeighborConstant(Edge edge)
{
  switch (edge)
  {
  case Edge::Top:
    return Cubios::NEIGHBOR_TOP;
  case Edge::Right:
    return Cubios::NEIGHBOR_RIGHT;
  case Edge::Bottom:
    return Cubios::NEIGHBOR_BOTTOM;
  case Edge::Left:
  default:
    return Cubios::NEIGHBOR_LEFT;
  }
}

TiltGridTest::NeighborDestination
TiltGridTest::resolveNeighborDestination(int screenID, Edge exitEdge,
                                         int tilePos) const
{
  NeighborDestination result{};

  const int module = screenID / 3;
  const int localScreen = screenID % 3;

  Cubios::TOPOLOGY_faceletInfo_t adjacent{};
  if (Cubios::TOPOLOGY_getAdjacentFacelet(module, localScreen,
                                          getNeighborConstant(exitEdge),
                                          &adjacent) != 0 ||
      !adjacent.connected)
  {
    return result;
  }

  if (adjacent.module < 0 || adjacent.module >= 8 || adjacent.screen < 0 ||
      adjacent.screen >= 3)
  {
    return result;
  }

  result.screenID = adjacent.module * 3 + adjacent.screen;
  if (result.screenID < 0 || result.screenID >= 24)
  {
    result.screenID = -1;
    return result;
  }

  int entryEdge = -1;
  for (int candidate = 0; candidate < 4; ++candidate)
  {
    Cubios::TOPOLOGY_faceletInfo_t backLink{};
    if (Cubios::TOPOLOGY_getAdjacentFacelet(
            adjacent.module, adjacent.screen,
            getNeighborConstant(static_cast<Edge>(candidate)),
            &backLink) == 0)
    {
      const int backLinkScreenID = backLink.module * 3 + backLink.screen;
      if (backLinkScreenID == screenID)
      {
        entryEdge = candidate;
        break;
      }
    }
  }

  if (entryEdge < 0)
  {
    entryEdge = (static_cast<int>(exitEdge) + 2) % 4;
  }

  result.entryEdge = static_cast<Edge>(entryEdge);

  const Edge perpA =
      (exitEdge == Edge::Top || exitEdge == Edge::Bottom) ? Edge::Left
                                                           : Edge::Top;
  const Edge perpBLo =
      (result.entryEdge == Edge::Top || result.entryEdge == Edge::Bottom)
          ? Edge::Left
          : Edge::Top;
  const Edge perpBHi =
      (result.entryEdge == Edge::Top || result.entryEdge == Edge::Bottom)
          ? Edge::Right
          : Edge::Bottom;

  bool needFlip = false;
  bool flipResolved = false;

  Cubios::TOPOLOGY_faceletInfo_t cornerA{};
  Cubios::TOPOLOGY_faceletInfo_t cornerB{};
  if (Cubios::TOPOLOGY_getAdjacentFacelet(module, localScreen,
                                          getNeighborConstant(perpA),
                                          &cornerA) == 0 &&
      cornerA.connected)
  {
    const int cornerAScreenID = cornerA.module * 3 + cornerA.screen;

    if (Cubios::TOPOLOGY_getAdjacentFacelet(adjacent.module, adjacent.screen,
                                            getNeighborConstant(perpBLo),
                                            &cornerB) == 0 &&
        cornerB.connected &&
        (cornerB.module * 3 + cornerB.screen) == cornerAScreenID)
    {
      needFlip = false;
      flipResolved = true;
    }
    else if (Cubios::TOPOLOGY_getAdjacentFacelet(adjacent.module,
                                                 adjacent.screen,
                                                 getNeighborConstant(perpBHi),
                                                 &cornerB) == 0 &&
             cornerB.connected &&
             (cornerB.module * 3 + cornerB.screen) == cornerAScreenID)
    {
      needFlip = true;
      flipResolved = true;
    }
  }

  if (!flipResolved)
  {
    const Edge perpAHi =
        (exitEdge == Edge::Top || exitEdge == Edge::Bottom) ? Edge::Right
                                                             : Edge::Bottom;
    if (Cubios::TOPOLOGY_getAdjacentFacelet(module, localScreen,
                                            getNeighborConstant(perpAHi),
                                            &cornerA) == 0 &&
        cornerA.connected)
    {
      const int cornerAScreenID = cornerA.module * 3 + cornerA.screen;

      if (Cubios::TOPOLOGY_getAdjacentFacelet(adjacent.module, adjacent.screen,
                                              getNeighborConstant(perpBLo),
                                              &cornerB) == 0 &&
          cornerB.connected &&
          (cornerB.module * 3 + cornerB.screen) == cornerAScreenID)
      {
        needFlip = true;
        flipResolved = true;
      }
      else if (Cubios::TOPOLOGY_getAdjacentFacelet(adjacent.module,
                                                   adjacent.screen,
                                                   getNeighborConstant(perpBHi),
                                                   &cornerB) == 0 &&
               cornerB.connected &&
               (cornerB.module * 3 + cornerB.screen) == cornerAScreenID)
      {
        needFlip = false;
        flipResolved = true;
      }
    }
  }

  result.entryPos = needFlip ? (kTilesPerScreen - 1 - tilePos) : tilePos;

  switch (result.entryEdge)
  {
  case Edge::Top:
    result.gridX = result.entryPos;
    result.gridY = 0;
    break;
  case Edge::Right:
    result.gridX = kTilesPerScreen - 1;
    result.gridY = result.entryPos;
    break;
  case Edge::Bottom:
    result.gridX = result.entryPos;
    result.gridY = kTilesPerScreen - 1;
    break;
  case Edge::Left:
  default:
    result.gridX = 0;
    result.gridY = result.entryPos;
    break;
  }

  result.valid = true;
  return result;
}

bool TiltGridTest::moveInsideCurrentScreen(int newX, int newY,
                                           uint32_t currentTime)
{
  this->lastMoveTime = currentTime;
  this->isMoving = true;
  this->moveElapsed = 0;
  this->moveFromX = this->renderX;
  this->moveFromY = this->renderY;
  this->moveToX = static_cast<float>(newX);
  this->moveToY = static_cast<float>(newY);
  this->gridX = newX;
  this->gridY = newY;
  return true;
}

bool TiltGridTest::moveAcrossScreen(Edge exitEdge, int tilePos,
                                    uint32_t currentTime)
{
  NeighborDestination dest =
      resolveNeighborDestination(this->currentScreenID, exitEdge, tilePos);
  if (!dest.valid)
  {
    return false;
  }

  this->currentScreenID = dest.screenID;
  this->gridX = dest.gridX;
  this->gridY = dest.gridY;
  this->syncOwnerFromCurrentScreen();
  this->snapRenderToGrid();
  this->isMoving = false;
  this->moveElapsed = 0;
  this->moveFromX = this->renderX;
  this->moveFromY = this->renderY;
  this->moveToX = this->renderX;
  this->moveToY = this->renderY;
  this->smoothAccelX = 0.0f;
  this->smoothAccelY = 0.0f;
  this->lastMoveTime = currentTime;
  this->broadcastPlayerPosition();
  return true;
}

bool TiltGridTest::tryStartMove(uint32_t currentTime)
{
  const int accelX = static_cast<int>(this->smoothAccelX);
  const int accelY = static_cast<int>(this->smoothAccelY);
  const int absAccelX = iabs32(accelX);
  const int absAccelY = iabs32(accelY);

  if (absAccelX <= kTiltThreshold && absAccelY <= kTiltThreshold)
  {
    return false;
  }

  int dx = 0;
  int dy = 0;

  if (absAccelX >= absAccelY)
  {
    dx = (accelX > 0) ? -1 : 1;
  }
  else
  {
    dy = (accelY > 0) ? -1 : 1;
  }

  const int newX = this->gridX + dx;
  const int newY = this->gridY + dy;

  if (newX >= 0 && newX < kTilesPerScreen && newY >= 0 &&
      newY < kTilesPerScreen)
  {
    return moveInsideCurrentScreen(newX, newY, currentTime);
  }

  Edge exitEdge = Edge::Top;
  int tilePos = 0;
  if (newY < 0)
  {
    exitEdge = Edge::Top;
    tilePos = this->gridX;
  }
  else if (newY >= kTilesPerScreen)
  {
    exitEdge = Edge::Bottom;
    tilePos = this->gridX;
  }
  else if (newX < 0)
  {
    exitEdge = Edge::Left;
    tilePos = this->gridY;
  }
  else
  {
    exitEdge = Edge::Right;
    tilePos = this->gridY;
  }

  return moveAcrossScreen(exitEdge, tilePos, currentTime);
}

void TiltGridTest::drawGridScreen(uint8_t renderTargetID) const
{
  Cubios::GFX_setRenderTarget(renderTargetID);
  Cubios::GFX_clear(kBackgroundColor);

  Cubios::GFX_drawRectangle(0, 0, kScreenSize, 2, kGridBorderColor);
  Cubios::GFX_drawRectangle(0, 0, 2, kScreenSize, kGridBorderColor);
  Cubios::GFX_drawRectangle(0, kScreenSize - 2, kScreenSize, 2,
                            kGridBorderColor);
  Cubios::GFX_drawRectangle(kScreenSize - 2, 0, 2, kScreenSize,
                            kGridBorderColor);

  Cubios::GFX_drawRectangle(kCellSize - kGridLineThickness / 2, 0,
                            kGridLineThickness, kScreenSize, kGridColor);
  Cubios::GFX_drawRectangle(0, kCellSize - kGridLineThickness / 2,
                            kScreenSize, kGridLineThickness, kGridColor);
}

void TiltGridTest::drawPlayer(uint8_t renderTargetID) const
{
  const int centerX = static_cast<int>(this->renderX * kCellSize) +
                      kCellCenterOffset;
  const int centerY = static_cast<int>(this->renderY * kCellSize) +
                      kCellCenterOffset;

  Cubios::GFX_setRenderTarget(renderTargetID);
  Cubios::GFX_drawSolidCircle(centerX + 4, centerY + 6, kCircleRadius,
                              kPlayerShadowColor);
  Cubios::GFX_drawSolidCircle(centerX, centerY, kCircleRadius, kPlayerFillColor);
  Cubios::GFX_drawSolidCircle(centerX - 10, centerY - 10, 8,
                              kPlayerHighlightColor);
}

void TiltGridTest::on_Tick(uint32_t currentTime, uint32_t deltaTime)
{
  if (this->isMoving)
  {
    this->moveElapsed += deltaTime;
    const float t =
        clamp01(static_cast<float>(this->moveElapsed) / kMoveAnimMS);

    this->renderX = this->moveFromX + (this->moveToX - this->moveFromX) * t;
    this->renderY = this->moveFromY + (this->moveToY - this->moveFromY) * t;

    if (t >= 1.0f)
    {
      this->isMoving = false;
      this->snapRenderToGrid();
      this->broadcastPlayerPosition();
    }
    return;
  }

  if (static_cast<int>(this->Module) != static_cast<int>(this->playerModule))
  {
    return;
  }

  if (currentTime - this->lastMoveTime < kMoveCooldownMS)
  {
    return;
  }

  tryStartMove(currentTime);
}

void TiltGridTest::on_Render(std::array<Cubios::Screen, 3> &screens)
{
  for (int i = 0; i < 3; ++i)
  {
    const uint8_t renderTargetID = screens[i].ID();
    drawGridScreen(renderTargetID);

    if (static_cast<int>(this->Module) == static_cast<int>(this->playerModule) &&
        renderTargetID == this->playerLocalScreen)
    {
      drawPlayer(renderTargetID);
    }

    Cubios::GFX_render();
  }
}

void TiltGridTest::on_PhysicsTick(const std::array<Cubios::Screen, 3> &)
{
  if (static_cast<int>(this->Module) != static_cast<int>(this->playerModule))
  {
    return;
  }

  const int32_t rawX = Cubios::MS_getFaceAccelX(this->playerLocalScreen);
  const int32_t rawY = Cubios::MS_getFaceAccelY(this->playerLocalScreen);

  this->smoothAccelX = this->smoothAccelX * (1.0f - kAccelSmoothAlpha) +
                       static_cast<float>(rawX) * kAccelSmoothAlpha;
  this->smoothAccelY = this->smoothAccelY * (1.0f - kAccelSmoothAlpha) +
                       static_cast<float>(rawY) * kAccelSmoothAlpha;
}

void TiltGridTest::on_Timer(uint8_t)
{
}

void TiltGridTest::on_Twist(const Cubios::TOPOLOGY_twistInfo_t &)
{
  const bool wasMoving = this->isMoving;
  this->isMoving = false;
  this->moveElapsed = 0;
  this->snapRenderToGrid();
  this->smoothAccelX = 0.0f;
  this->smoothAccelY = 0.0f;

  if (wasMoving &&
      static_cast<int>(this->Module) == static_cast<int>(this->playerModule))
  {
    this->broadcastPlayerPosition();
  }
}

void TiltGridTest::on_Message(uint32_t type, uint8_t *pkt, u32_t size)
{
  if (type != kPlayerPositionMessageType || pkt == nullptr ||
      size < sizeof(PlayerPositionPacket))
  {
    return;
  }

  const PlayerPositionPacket *playerPkt =
      reinterpret_cast<const PlayerPositionPacket *>(pkt);
  if (playerPkt->moduleID >= 8 || playerPkt->screenIndex >= 3 ||
      playerPkt->gridX >= kTilesPerScreen ||
      playerPkt->gridY >= kTilesPerScreen)
  {
    return;
  }

  const int previousScreenID = this->currentScreenID;

  this->playerModule = playerPkt->moduleID;
  this->playerLocalScreen = playerPkt->screenIndex;
  this->currentScreenID = static_cast<int>(this->playerModule) * 3 +
                          static_cast<int>(this->playerLocalScreen);
  this->gridX = static_cast<int>(playerPkt->gridX);
  this->gridY = static_cast<int>(playerPkt->gridY);
  this->isMoving = false;
  this->moveElapsed = 0;
  this->snapRenderToGrid();

  if (this->currentScreenID != previousScreenID)
  {
    this->smoothAccelX = 0.0f;
    this->smoothAccelY = 0.0f;
  }
}

void TiltGridTest::on_ExternalMessage(uint8_t *, u32_t)
{
}

void TiltGridTest::on_Close()
{
}
TiltGridTest.cpp
15 KB
