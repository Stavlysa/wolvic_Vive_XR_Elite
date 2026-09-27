package com.igalia.wolvic.input;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import android.util.SparseArray;
import com.igalia.wolvic.ui.widgets.Widget;
import org.junit.After;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.annotation.Config;
import java.lang.reflect.Field;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;

@RunWith(RobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class MotionEventGeneratorTest {
    @After public void cleanup() { MotionEventGenerator.clearDevices(); }

    @Test public void sparseHandIdsDoNotAllowTwoSimultaneousTouches() throws Exception {
        MotionEventGenerator.clearDevices();
        Field field = MotionEventGenerator.class.getDeclaredField("devices");
        field.setAccessible(true);
        @SuppressWarnings("unchecked")
        SparseArray<MotionEventGenerator.Device> devices =
                (SparseArray<MotionEventGenerator.Device>) field.get(null);
        MotionEventGenerator.Device right = new MotionEventGenerator.Device(2);
        MotionEventGenerator.Device left = new MotionEventGenerator.Device(3);
        devices.put(2, right);
        devices.put(3, left);
        Method check = MotionEventGenerator.class.getDeclaredMethod("isOtherDeviceDown", int.class);
        check.setAccessible(true);
        assertFalse((boolean) check.invoke(null, 2));
        left.mTouchStartWidget = (Widget) Proxy.newProxyInstance(Widget.class.getClassLoader(),
                new Class<?>[]{Widget.class}, (proxy, method, args) -> null);
        assertTrue((boolean) check.invoke(null, 2));
        assertFalse((boolean) check.invoke(null, 3));
        left.mTouchStartWidget = null;
        right.mTouchStartWidget = (Widget) Proxy.newProxyInstance(Widget.class.getClassLoader(),
                new Class<?>[]{Widget.class}, (proxy, method, args) -> null);
        assertTrue((boolean) check.invoke(null, 3));
    }
}
