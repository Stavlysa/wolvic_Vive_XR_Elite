package com.igalia.wolvic.utils;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class AppUpdateManagerTest {
    private static final int INSTALLED_VERSION = 202031534;

    private static final String RELEASES_JSON = "["
            + "{\"name\":\"Prerelease 57\",\"html_url\":\"https://github.com/Stavlysa/wolvic/releases/57\","
            + "\"draft\":false,\"prerelease\":true,\"assets\":[{\"name\":\"Wolvic-stage-57-v202031700.apk\"}]},"
            + "{\"name\":\"Release 56.1\",\"html_url\":\"https://github.com/Stavlysa/wolvic/releases/56-1\","
            + "\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"Wolvic-v202031600.apk\"}]}"
            + "]";

    @Test
    public void releaseChannelIgnoresPrereleases() {
        AppUpdateManager.Result result = AppUpdateManager.parseReleaseList(
                RELEASES_JSON, false, INSTALLED_VERSION);

        assertEquals(AppUpdateManager.Status.UPDATE_AVAILABLE, result.getStatus());
        assertNotNull(result.getUpdateInfo());
        assertEquals(202031600, result.getUpdateInfo().getVersionCode());
        assertFalse(result.getUpdateInfo().isPrerelease());
    }

    @Test
    public void combinedChannelIncludesPrereleases() {
        AppUpdateManager.Result result = AppUpdateManager.parseReleaseList(
                RELEASES_JSON, true, INSTALLED_VERSION);

        assertEquals(AppUpdateManager.Status.UPDATE_AVAILABLE, result.getStatus());
        assertNotNull(result.getUpdateInfo());
        assertEquals(202031700, result.getUpdateInfo().getVersionCode());
        assertTrue(result.getUpdateInfo().isPrerelease());
    }

    @Test
    public void fallsBackToVersionCodeInBodyWhenApkNameHasNone() {
        String json = "[{\"name\":\"Release\","
                + "\"html_url\":\"https://github.com/Stavlysa/wolvic/releases/new\","
                + "\"draft\":false,\"prerelease\":false,"
                + "\"body\":\"Android versionCode: `202031800`\","
                + "\"assets\":[{\"name\":\"Wolvic-vive-xr-elite.apk\"}]}]";

        AppUpdateManager.Result result = AppUpdateManager.parseReleaseList(
                json, false, INSTALLED_VERSION);

        assertEquals(AppUpdateManager.Status.UPDATE_AVAILABLE, result.getStatus());
        assertNotNull(result.getUpdateInfo());
        assertEquals(202031800, result.getUpdateInfo().getVersionCode());
    }

    @Test
    public void olderAndDraftBuildsDoNotProduceAnUpdate() {
        String json = "["
                + "{\"name\":\"Old\",\"html_url\":\"https://github.com/Stavlysa/wolvic/releases/old\","
                + "\"draft\":false,\"prerelease\":false,\"assets\":[{\"name\":\"Wolvic-v202031100.apk\"}]},"
                + "{\"name\":\"Draft\",\"html_url\":\"https://github.com/Stavlysa/wolvic/releases/draft\","
                + "\"draft\":true,\"prerelease\":false,\"assets\":[{\"name\":\"Wolvic-v202031900.apk\"}]}"
                + "]";

        AppUpdateManager.Result result = AppUpdateManager.parseReleaseList(
                json, true, INSTALLED_VERSION);

        assertEquals(AppUpdateManager.Status.UP_TO_DATE, result.getStatus());
        assertNull(result.getUpdateInfo());
    }
}
