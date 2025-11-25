# Tiny-Splatoon

## Project Structure

- 本專案以 C++ / OpenGL 為主，實作一個類似「斯普拉頓塗地模式」的技術展示。
- 架構採用分層式設計，將渲染、遊戲邏輯、塗地系統完全分離，以提升可維護性與擴充性。

```
Tiny-Splatoon/
│
├── engine/          # 引擎基礎（OpenGL、資源管理、系統）
├── gameplay/        # 遊戲邏輯（玩家、武器、墨水子彈）
├── splat/           # 塗地系統（塗地地圖、Painter、Physics）
├── scene/           # 地板 / 場景物件
└── main.cpp         # 程式入口
```

## Module Overview

### 1. engine

> 提供低階系統、OpenGL 操作、資源管理

#### core

- Window：建立 OpenGL 視窗與上下文、輸入事件處理
- Input：鍵盤、滑鼠輸入偵測
- Timer：計算 deltaTime、FPS
- Logger：統一 log 輸出

#### rendering

- Shader：加載、編譯、綁定 Shader
- Texture：貼圖加載、綁定、支援 HDR / 高精度格式（如 RGBA32F）
- Mesh：儲存 VBO、VAO、IBO
- Camera：控制視角、投影
- Renderer：統一場景渲染流程、負責繪製所有 Mesh

### 2. gameplay

> 包含玩家、武器、子彈邏輯，不直接接觸 OpenGL。

- Player：玩家位置、朝向、移動控制，武器觸發事件
- Weapon：控制噴射墨水行為（射速、噴射角度）、產生 Projectile
- Projectile：表示墨水子彈、計算飛行、撞擊判定後生成塗地事件
	- 回傳命中 UV：傳給 SplatPainter
- GameWorld：管理所有 gameplay 物件（player、projectile、武器）


### 3. splat

> 此區負責整個墨水塗地技術實作。

- SplatMap：儲存地板上的塗色緩衝區（RGBA32F Texture）
	- 回傳 texture handle 給渲染器使用
- SplatPainter：把墨水畫上地板、核心塗地技術（brush / decal）
	- 回傳更新後的 splat texture
- SplatPhysics：計算墨水撞擊地板的位置與 UV、角色移動拖地、射擊角度 → 正確落點
	- 根據子彈射線及地板 Mesh，回傳 Painter 的 UV
- SplatRenderer：將 SplatMap（顏色）與地板原始材質混色後輸出
	- 根據 splat texture、base texture 及地板 UV，回傳最終地板顯示結果（fragment color）

### 4. scene

- Level：載入場景模型（地板、障礙物）、提供碰撞資訊
- FloorMesh：地板的 mesh（必須有 UV）、提供給 SplatPhysics 用來轉換 UV 座標
	- 回傳 mesh + UV
- Entity：所有場景物件的基本類別

## Module Interaction Diagram

```
Player Input → Gameplay → Projectile
                         ↓ hits
SplatPhysics → UV → SplatPainter
                         ↓ writes
                    SplatMap (Texture)
                         ↓ reads
                  SplatRenderer → OpenGL畫面
```
