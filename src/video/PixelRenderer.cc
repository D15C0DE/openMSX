// $Id$

/*
TODO:
- Implement blinking (of page mask) in bitmap modes.
*/

#include "PixelRenderer.hh"
#include "Rasterizer.hh"
#include "Display.hh"
#include "VideoSystem.hh"
#include "RenderSettings.hh"
#include "IntegerSetting.hh"
#include "BooleanSetting.hh"
#include "VDP.hh"
#include "VDPVRAM.hh"
#include "SpriteChecker.hh"
#include "EventDistributor.hh"
#include "FinishFrameEvent.hh"
#include "RealTime.hh"
#include "MSXMotherBoard.hh"
#include "Timer.hh"
#include <algorithm>
#include <cassert>
#include <sstream>

namespace openmsx {

/** Line number where top border starts.
  * This is independent of PAL/NTSC timing or number of lines per screen.
  */
static const int LINE_TOP_BORDER = 3 + 13;

void PixelRenderer::draw(
	int startX, int startY, int endX, int endY, DrawType drawType, bool atEnd)
{
	if (drawType == DRAW_BORDER) {
		rasterizer->drawBorder(startX, startY, endX, endY);
	} else {
		assert(drawType == DRAW_DISPLAY);

		// Calculate display coordinates.
		int zero = vdp.getLineZero();
		int displayX = (startX - vdp.getLeftSprites()) / 2;
		int displayY = startY - zero;
		if (!vdp.getDisplayMode().isTextMode()) {
			displayY += vdp.getVerticalScroll();
		} else {
			// this is not what the real VDP does, but it is good
			// enough for "Boring scroll" demo part of "Relax"
			displayY = (displayY & 7) | (textModeCounter * 8);
			if (atEnd && (drawType == DRAW_DISPLAY)) {
				int low  = std::max(0, (startY - zero)) / 8;
				int high = std::max(0, (endY   - zero)) / 8;
				textModeCounter += (high - low);
			}
		}

		displayY &= 255; // Page wrap.
		int displayWidth = (endX - (startX & ~1)) / 2;
		int displayHeight = endY - startY;

		assert(0 <= displayX);
		assert(displayX + displayWidth <= 512);

		rasterizer->drawDisplay(
			startX, startY,
			displayX - vdp.getHorizontalScrollLow() * 2, displayY,
			displayWidth, displayHeight
			);
		if (vdp.spritesEnabled()) {
			rasterizer->drawSprites(
				startX, startY,
				displayX / 2, displayY,
				(displayWidth + 1) / 2, displayHeight
				);
		}
	}
}

void PixelRenderer::subdivide(
	int startX, int startY, int endX, int endY, int clipL, int clipR,
	DrawType drawType )
{
	// Partial first line.
	if (startX > clipL) {
		bool atEnd = (startY != endY) || (endX >= clipR);
		if (startX < clipR) {
			draw(startX, startY, (atEnd ? clipR : endX),
			     startY + 1, drawType, atEnd);
		}
		if (startY == endY) return;
		startY++;
	}
	// Partial last line.
	bool drawLast = false;
	if (endX >= clipR) {
		endY++;
	} else if (endX > clipL) {
		drawLast = true;
	}
	// Full middle lines.
	if (startY < endY) {
		draw(clipL, startY, clipR, endY, drawType, true);
	}
	// Actually draw last line if necessary.
	// The point of keeping top-to-bottom draw order is that it increases
	// the locality of memory references, which generally improves cache
	// hit rates.
	if (drawLast) draw(clipL, endY, endX, endY + 1, drawType, false);
}

PixelRenderer::PixelRenderer(VDP& vdp_)
	: vdp(vdp_), vram(vdp.getVRAM())
	, eventDistributor(vdp.getMotherBoard().getEventDistributor())
	, realTime(vdp.getMotherBoard().getRealTime())
	, renderSettings(vdp.getMotherBoard().getRenderSettings())
	, spriteChecker(vdp.getSpriteChecker())
	, rasterizer(vdp.getMotherBoard().getDisplay().getVideoSystem().
	             createRasterizer(vdp))
{
	frameSkipCounter = 999; // force drawing of frame
	finishFrameDuration = 0;
	prevDrawFrame = drawFrame = renderFrame = false; // don't draw before frameStart is called
	displayEnabled = vdp.isDisplayEnabled();
	rasterizer->reset();

	renderSettings.getMaxFrameSkip().addListener(this);
	renderSettings.getMinFrameSkip().addListener(this);
}

PixelRenderer::~PixelRenderer()
{
	renderSettings.getMinFrameSkip().removeListener(this);
	renderSettings.getMaxFrameSkip().removeListener(this);
}

void PixelRenderer::reset(const EmuTime& time)
{
	rasterizer->reset();
	displayEnabled = vdp.isDisplayEnabled();
	frameStart(time);
}

void PixelRenderer::updateDisplayEnabled(bool enabled, const EmuTime& time)
{
	sync(time, true);
	displayEnabled = enabled;
}

void PixelRenderer::frameStart(const EmuTime& time)
{
	bool draw = false;
	if (!rasterizer->isActive()) {
		frameSkipCounter = 0;
	} else if (frameSkipCounter < renderSettings.getMinFrameSkip().getValue()) {
		++frameSkipCounter;
	} else if (frameSkipCounter >= renderSettings.getMaxFrameSkip().getValue()) {
		frameSkipCounter = 0;
		draw = true;
	} else {
		++frameSkipCounter;
		draw = realTime.timeLeft((unsigned)finishFrameDuration, time);
		if (draw) {
			frameSkipCounter = 0;
		}
	}
	prevDrawFrame = drawFrame;
	drawFrame = draw;
	renderFrame = drawFrame ||
	     (prevDrawFrame && vdp.isInterlaced() &&
	      renderSettings.getDeinterlace().getValue());
	if (!renderFrame) return;

	rasterizer->frameStart();

	accuracy = renderSettings.getAccuracy().getValue();

	nextX = 0;
	nextY = 0;
	// This is not what the real VDP does, but it is good enough
	// for the "Boring scroll" demo part of ANMA's "Relax" demo.
	textModeCounter = 0;
}

void PixelRenderer::frameEnd(const EmuTime& time)
{
	if (renderFrame) {
		// Render changes from this last frame.
		sync(time, true);

		// Let underlying graphics system finish rendering this frame.
		unsigned long long time1 = Timer::getTime();
		rasterizer->frameEnd();
		unsigned long long time2 = Timer::getTime();
		unsigned long long current = time2 - time1;
		const double ALPHA = 0.2;
		finishFrameDuration = finishFrameDuration * (1 - ALPHA) +
		                      current * ALPHA;

		if (drawFrame) {
			FinishFrameEvent* f = new FinishFrameEvent(VIDEO_MSX);
			eventDistributor.distributeEvent(f);
		}
	}
}

void PixelRenderer::updateHorizontalScrollLow(
	byte /*scroll*/, const EmuTime& time
) {
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateHorizontalScrollHigh(
	byte /*scroll*/, const EmuTime& time
) {
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateBorderMask(
	bool /*masked*/, const EmuTime& time
) {
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateMultiPage(
	bool /*multiPage*/, const EmuTime& time
) {
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateTransparency(
	bool enabled, const EmuTime& time)
{
	if (displayEnabled) sync(time);
	rasterizer->setTransparency(enabled);
}

void PixelRenderer::updateForegroundColour(
	int /*colour*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateBackgroundColour(
	int colour, const EmuTime& time)
{
	sync(time);
	if (vdp.getDisplayMode().getByte() != DisplayMode::GRAPHIC7) {
		rasterizer->setBackgroundColour(colour);
	}
}

void PixelRenderer::updateBlinkForegroundColour(
	int /*colour*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateBlinkBackgroundColour(
	int /*colour*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateBlinkState(
	bool /*enabled*/, const EmuTime& /*time*/)
{
	// TODO: When the sync call is enabled, the screen flashes on
	//       every call to this method.
	//       I don't know why exactly, but it's probably related to
	//       being called at frame start.
	//sync(time);
}

void PixelRenderer::updatePalette(
	int index, int grb, const EmuTime& time)
{
	if (displayEnabled) {
		sync(time);
	} else {
		// Only sync if border colour changed.
		DisplayMode mode = vdp.getDisplayMode();
		if (mode.getBase() == DisplayMode::GRAPHIC5) {
			int bgColour = vdp.getBackgroundColour();
			if (index == (bgColour & 3) || (index == (bgColour >> 2))) {
				sync(time);
			}
		} else if (mode.getByte() != DisplayMode::GRAPHIC7) {
			if (index == vdp.getBackgroundColour()) {
				sync(time);
			}
		}
	}
	rasterizer->setPalette(index, grb);
}

void PixelRenderer::updateVerticalScroll(
	int /*scroll*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateHorizontalAdjust(
	int /*adjust*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateDisplayMode(
	DisplayMode mode, const EmuTime& time)
{
	// Sync if in display area or if border drawing process changes.
	DisplayMode oldMode = vdp.getDisplayMode();
	if (displayEnabled
	|| oldMode.getByte() == DisplayMode::GRAPHIC5
	|| oldMode.getByte() == DisplayMode::GRAPHIC7
	|| mode.getByte() == DisplayMode::GRAPHIC5
	|| mode.getByte() == DisplayMode::GRAPHIC7) {
		sync(time, true);
	}
	rasterizer->setDisplayMode(mode);
}

void PixelRenderer::updateNameBase(
	int /*addr*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updatePatternBase(
	int /*addr*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateColourBase(
	int /*addr*/, const EmuTime& time)
{
	if (displayEnabled) sync(time);
}

void PixelRenderer::updateSpritesEnabled(
	bool /*enabled*/, const EmuTime& time
) {
	if (displayEnabled) sync(time);
}

static inline bool overlap(
	int displayY0, // start of display region, inclusive
	int displayY1, // end of display region, exclusive
	int vramLine0, // start of VRAM region, inclusive
	int vramLine1  // end of VRAM region, exclusive
	// Note: Display region can wrap around: 256 -> 0.
	//       VRAM region cannot wrap around.
) {
	if (displayY0 <= displayY1) {
		if (vramLine1 > displayY0) {
			if (vramLine0 <= displayY1) return true;
		}
	} else {
		if (vramLine1 > displayY0) return true;
		if (vramLine0 <= displayY1) return true;
	}
	return false;
}

inline bool PixelRenderer::checkSync(int offset, const EmuTime& time)
{
	// TODO: Because range is entire VRAM, offset == address.

	// If display is disabled, VRAM changes will not affect the
	// renderer output, therefore sync is not necessary.
	// TODO: Have bitmapVisibleWindow disabled in this case.
	if (!displayEnabled) return false;
	//if (frameSkipCounter != 0) return false; // TODO
	if (accuracy == RenderSettings::ACC_SCREEN) return false;

	// Calculate what display lines are scanned between current
	// renderer time and update-to time.
	// Note: displayY1 is inclusive.
	int deltaY = vdp.getVerticalScroll() - vdp.getLineZero();
	int limitY = vdp.getTicksThisFrame(time) / VDP::TICKS_PER_LINE;
	int displayY0 = (nextY + deltaY) & 255;
	int displayY1 = (limitY + deltaY) & 255;

	switch(vdp.getDisplayMode().getBase()) {
	case DisplayMode::GRAPHIC2:
	case DisplayMode::GRAPHIC3:
		if (vram.colourTable.isInside(offset)) {
			int vramQuarter = (offset & 0x1800) >> 11;
			int mask = (vram.colourTable.getMask() & 0x1800) >> 11;
			for (int i = 0; i < 4; i++) {
				if ( (i & mask) == vramQuarter
				&& overlap(displayY0, displayY1, i * 64, (i + 1) * 64) ) {
					/*fprintf(stderr,
						"colour table: %05X %04X - quarter %d\n",
						offset, offset & 0x1FFF, i
						);*/
					return true;
				}
			}
		}
		if (vram.patternTable.isInside(offset)) {
			int vramQuarter = (offset & 0x1800) >> 11;
			int mask = (vram.patternTable.getMask() & 0x1800) >> 11;
			for (int i = 0; i < 4; i++) {
				if ( (i & mask) == vramQuarter
				&& overlap(displayY0, displayY1, i * 64, (i + 1) * 64) ) {
					/*fprintf(stderr,
						"pattern table: %05X %04X - quarter %d\n",
						offset, offset & 0x1FFF, i
						);*/
					return true;
				}
			}
		}
		if (vram.nameTable.isInside(offset)) {
			int vramLine = ((offset & 0x3FF) / 32) * 8;
			if (overlap(displayY0, displayY1, vramLine, vramLine + 8)) {
				/*fprintf(stderr,
					"name table: %05X %03X - line %d\n",
					offset, offset & 0x3FF, vramLine
					);*/
				return true;
			}
		}
		return false;
	case DisplayMode::GRAPHIC4:
	case DisplayMode::GRAPHIC5: {
		// Is the address inside the visual page(s)?
		// TODO: Also look at which lines are touched inside pages.
		int visiblePage = vram.nameTable.getMask()
			& (0x10000 | (vdp.getEvenOddMask() << 7));
		if (vdp.isMultiPageScrolling()) {
			return (offset & 0x18000) == visiblePage
				|| (offset & 0x18000) == (visiblePage & 0x10000);
		} else {
			return (offset & 0x18000) == visiblePage;
		}
	}
	case DisplayMode::GRAPHIC6:
	case DisplayMode::GRAPHIC7:
		return true; // TODO: Implement better detection.
	default:
		// Range unknown; assume full range.
		return vram.nameTable.isInside(offset)
			|| vram.colourTable.isInside(offset)
			|| vram.patternTable.isInside(offset);
	}
}

void PixelRenderer::updateVRAM(unsigned offset, const EmuTime& time)
{
	// Note: No need to sync if display is disabled, because then the
	//       output does not depend on VRAM (only on background colour).
	if (renderFrame && displayEnabled && checkSync(offset, time)) {
		/*
		fprintf(stderr, "vram sync @ line %d\n",
			vdp.getTicksThisFrame(time) / VDP::TICKS_PER_LINE
			);
		*/
		renderUntil(time);
	}
	rasterizer->updateVRAMCache(offset);
}

void PixelRenderer::updateWindow(bool /*enabled*/, const EmuTime& /*time*/)
{
	// The bitmapVisibleWindow has moved to a different area.
	// This update is redundant: Renderer will be notified in another way
	// as well (updateDisplayEnabled or updateNameBase, for example).
	// TODO: Can this be used as the main update method instead?
}

void PixelRenderer::sync(const EmuTime& time, bool force)
{
	if (!renderFrame) return;

	// Synchronisation is done in two phases:
	// 1. update VRAM
	// 2. update other subsystems
	// Note that as part of step 1, type 2 updates can be triggered.
	// Executing step 2 takes care of the subsystem changes that occur
	// after the last VRAM update.
	// This scheme makes sure type 2 routines such as renderUntil and
	// checkUntil are not re-entered, which was causing major pain in
	// the past.
	// TODO: I wonder if it's possible to enforce this synchronisation
	//       scheme at a higher level. Probably. But how...
	//if ((frameSkipCounter == 0) && TODO
	if (accuracy != RenderSettings::ACC_SCREEN || force) {
		vram.sync(time);
		renderUntil(time);
	}
}

void PixelRenderer::renderUntil(const EmuTime& time)
{
	// Translate from time to pixel position.
	int limitTicks = vdp.getTicksThisFrame(time);
	assert(limitTicks <= vdp.getTicksPerFrame());
	int limitX, limitY;
	switch (accuracy) {
	case RenderSettings::ACC_PIXEL: {
		limitX = limitTicks % VDP::TICKS_PER_LINE;
		limitY = limitTicks / VDP::TICKS_PER_LINE;
		break;
	}
	case RenderSettings::ACC_LINE:
	case RenderSettings::ACC_SCREEN: {
		// Note: I'm not sure the rounding point is optimal.
		//       It used to be based on the left margin, but that doesn't work
		//       because the margin can change which leads to a line being
		//       rendered even though the time doesn't advance.
		limitX = 0;
		limitY =
			(limitTicks + VDP::TICKS_PER_LINE - 400) / VDP::TICKS_PER_LINE;
		break;
	}
	default:
		assert(false);
		limitX = limitY = 0; // avoid warning
	}

	// Stop here if there is nothing to render.
	// This ensures that no pixels are rendered in a series of updates that
	// happen at exactly the same time; the VDP subsystem states may be
	// inconsistent until all updates are performed.
	// Also it is a small performance optimisation.
	if (limitX == nextX && limitY == nextY) return;

	if (displayEnabled) {
		if (vdp.spritesEnabled()) {
			// Update sprite checking, so that rasterizer can call getSprites.
			spriteChecker.checkUntil(time);
		}

		// Calculate start and end of borders in ticks since start of line.
		// The 0..7 extra horizontal scroll low pixels should be drawn in
		// border colour. These will be drawn together with the border,
		// but sprites above these pixels are clipped at the actual border
		// rather than the end of the border coloured area.
		// TODO: Move these calculations and getDisplayLeft() to VDP.
		int borderL = vdp.getLeftBorder();
		int displayL =
			vdp.isBorderMasked() ? borderL : vdp.getLeftBackground();
		int borderR = vdp.getRightBorder();

		// Left border.
		subdivide(nextX, nextY, limitX, limitY,
			0, displayL, DRAW_BORDER );
		// Display area.
		subdivide(nextX, nextY, limitX, limitY,
			displayL, borderR, DRAW_DISPLAY );
		// Right border.
		subdivide(nextX, nextY, limitX, limitY,
			borderR, VDP::TICKS_PER_LINE, DRAW_BORDER );
	} else {
		subdivide(nextX, nextY, limitX, limitY,
			0, VDP::TICKS_PER_LINE, DRAW_BORDER );
	}

	nextX = limitX;
	nextY = limitY;
}

void PixelRenderer::update(const Setting* setting)
{
	if (setting == &renderSettings.getMinFrameSkip()
	|| setting == &renderSettings.getMaxFrameSkip() ) {
		// Force drawing of frame.
		frameSkipCounter = 999;
	} else {
		assert(false);
	}
}

} // namespace openmsx
