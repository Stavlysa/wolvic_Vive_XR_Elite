/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package com.igalia.wolvic;

import android.Manifest;
import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.view.KeyEvent;

import com.htc.vr.sdk.VRActivity;
import com.igalia.wolvic.ui.widgets.WidgetManagerDelegate;

/** HTC Wave lifecycle bridge used by the VIVE XR Elite Wave build. */
public class PlatformActivity extends VRActivity {

    public static boolean filterPermission(final String aPermission) {
        return Manifest.permission.CAMERA.equals(aPermission);
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
        return null;
    }

    public PlatformActivity() {}

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        queueRunnable(() -> initializeJava(getAssets()));
    }

    @Override
    public void onBackPressed() {
        // Wave reserves the system back/menu path for its own VR shell.
    }

    @Override
    protected void onPause() {
        queueRunnable(() -> setPassthroughPausedNative(true));
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        queueRunnable(() -> setPassthroughPausedNative(false));
    }

    protected native void queueRunnable(Runnable aRunnable);
    protected native void initializeJava(AssetManager aAssets);
    private native void setPassthroughPausedNative(boolean paused);
}
