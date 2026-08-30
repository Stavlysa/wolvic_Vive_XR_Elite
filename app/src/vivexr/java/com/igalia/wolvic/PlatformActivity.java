/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package com.igalia.wolvic;

import android.content.Intent;
import android.view.KeyEvent;

import com.google.androidgamesdk.GameActivity;
import com.igalia.wolvic.ui.widgets.WidgetManagerDelegate;

/** Platform hooks for the standalone VIVE XR Elite OpenXR build. */
public class PlatformActivity extends GameActivity {

    public static boolean filterPermission(final String aPermission) {
        return false;
    }

    public static boolean isNotSpecialKey(KeyEvent event) {
        return true;
    }

    public static boolean isPositionTrackingSupported() {
        return true;
    }

    public final PlatformActivityPlugin createPlatformPlugin(WidgetManagerDelegate delegate) {
        return null;
    }

    protected Intent getStoreIntent() {
        return null;
    }

    protected String getEyeTrackingPermissionString() {
        // VIVE's OpenXR runtime gates XR_EXT_eye_gaze_interaction itself and
        // does not expose a separate Android runtime permission to request.
        return null;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        if (keyCode == KeyEvent.KEYCODE_BACK) {
            // GameActivity does not invoke onBackPressed() on KEYCODE_BACK.
            onBackPressed();
        }
        return super.onKeyUp(keyCode, event);
    }

    protected native void queueRunnable(Runnable aRunnable);
    protected native boolean platformExit();
}
