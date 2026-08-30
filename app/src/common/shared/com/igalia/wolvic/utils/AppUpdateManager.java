/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package com.igalia.wolvic.utils;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.preference.PreferenceManager;

import com.igalia.wolvic.BuildConfig;
import com.igalia.wolvic.R;
import com.igalia.wolvic.VRBrowserApplication;
import com.igalia.wolvic.browser.SettingsStore;
import com.google.gson.JsonArray;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/** Checks the VIVE XR Elite fork's public GitHub releases without downloading an APK. */
public final class AppUpdateManager {
    public static final String REPOSITORY_URL =
            "https://github.com/Stavlysa/wolvic_Vive_XR_Elite";
    private static final String RELEASES_API_URL =
            "https://api.github.com/repos/Stavlysa/wolvic_Vive_XR_Elite/releases?per_page=30";
    private static final long AUTO_CHECK_INTERVAL_MS = TimeUnit.HOURS.toMillis(24);
    private static final int NETWORK_TIMEOUT_MS = 10_000;
    private static final Pattern APK_VERSION_CODE_PATTERN = Pattern.compile(
            "(?:^|[^0-9])v(\\d{6,10})(?=[^0-9]|$)", Pattern.CASE_INSENSITIVE);
    private static final Pattern BODY_VERSION_CODE_PATTERN = Pattern.compile(
            "version\\s*code[^0-9]{0,24}(\\d{6,10})", Pattern.CASE_INSENSITIVE);

    public enum Status {
        UPDATE_AVAILABLE,
        UP_TO_DATE,
        ERROR,
        SKIPPED
    }

    public interface Callback {
        void onResult(@NonNull Result result);
    }

    public static final class UpdateInfo {
        private final int mVersionCode;
        private final String mTitle;
        private final String mHtmlUrl;
        private final boolean mPrerelease;

        UpdateInfo(int versionCode, @NonNull String title, @NonNull String htmlUrl,
                   boolean prerelease) {
            mVersionCode = versionCode;
            mTitle = title;
            mHtmlUrl = htmlUrl;
            mPrerelease = prerelease;
        }

        public int getVersionCode() {
            return mVersionCode;
        }

        @NonNull
        public String getTitle() {
            return mTitle;
        }

        @NonNull
        public String getHtmlUrl() {
            return mHtmlUrl;
        }

        public boolean isPrerelease() {
            return mPrerelease;
        }
    }

    public static final class Result {
        private final Status mStatus;
        private final UpdateInfo mUpdateInfo;

        private Result(@NonNull Status status, @Nullable UpdateInfo updateInfo) {
            mStatus = status;
            mUpdateInfo = updateInfo;
        }

        @NonNull
        public Status getStatus() {
            return mStatus;
        }

        @Nullable
        public UpdateInfo getUpdateInfo() {
            return mUpdateInfo;
        }
    }

    private AppUpdateManager() {}

    /**
     * Checks GitHub on Wolvic's background executor and returns on the main thread.
     * Automatic checks obey the user toggle and the 24-hour interval; forced checks do not.
     */
    public static void checkForUpdates(@NonNull Context context, boolean force,
                                       @Nullable Callback callback) {
        Context appContext = context.getApplicationContext();
        SettingsStore settings = SettingsStore.getInstance(appContext);
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(appContext);
        long now = System.currentTimeMillis();

        if (!force && (!settings.isUpdateCheckEnabled()
                || now - getLastCheckTime(appContext, prefs) < AUTO_CHECK_INTERVAL_MS)) {
            dispatch(appContext, callback, new Result(Status.SKIPPED, null));
            return;
        }

        boolean includePrereleases = settings.isUpdateIncludePrereleasesEnabled();
        ((VRBrowserApplication) appContext).getExecutors().backgroundThread().post(() -> {
            Result result;
            HttpURLConnection connection = null;
            try {
                connection = (HttpURLConnection) new URL(RELEASES_API_URL).openConnection();
                connection.setInstanceFollowRedirects(true);
                connection.setConnectTimeout(NETWORK_TIMEOUT_MS);
                connection.setReadTimeout(NETWORK_TIMEOUT_MS);
                connection.setRequestProperty("Accept", "application/vnd.github+json");
                connection.setRequestProperty("X-GitHub-Api-Version", "2022-11-28");
                connection.setRequestProperty(
                        "User-Agent", "Wolvic-VIVE-XR-Elite/" + BuildConfig.VERSION_NAME);

                int responseCode = connection.getResponseCode();
                if (responseCode != HttpURLConnection.HTTP_OK) {
                    throw new IOException("GitHub returned HTTP " + responseCode);
                }

                try (InputStream stream = connection.getInputStream()) {
                    result = parseReleaseList(
                            readUtf8(stream), includePrereleases, BuildConfig.VERSION_CODE);
                }
                saveSuccessfulResult(appContext, prefs, result, System.currentTimeMillis());
            } catch (IOException | RuntimeException exception) {
                result = new Result(Status.ERROR, null);
            } finally {
                if (connection != null) {
                    connection.disconnect();
                }
            }

            Result completedResult = result;
            dispatch(appContext, callback, completedResult);
        });
    }

    @Nullable
    public static UpdateInfo getCachedUpdate(@NonNull Context context) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        int versionCode = prefs.getInt(
                context.getString(R.string.settings_key_update_cached_version_code), 0);
        String title = prefs.getString(
                context.getString(R.string.settings_key_update_cached_title), "");
        String htmlUrl = prefs.getString(
                context.getString(R.string.settings_key_update_cached_url), "");
        boolean prerelease = prefs.getBoolean(
                context.getString(R.string.settings_key_update_cached_prerelease), false);
        boolean includePrereleases = SettingsStore.getInstance(context)
                .isUpdateIncludePrereleasesEnabled();

        if (versionCode <= BuildConfig.VERSION_CODE || title == null || title.isEmpty()
                || htmlUrl == null || htmlUrl.isEmpty()
                || (prerelease && !includePrereleases)) {
            return null;
        }
        return new UpdateInfo(versionCode, title, htmlUrl, prerelease);
    }

    public static boolean shouldNotify(@NonNull Context context, @NonNull UpdateInfo info) {
        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(context);
        return info.getVersionCode() > prefs.getInt(
                context.getString(R.string.settings_key_update_last_notified_version_code), 0);
    }

    public static void markNotified(@NonNull Context context, @NonNull UpdateInfo info) {
        PreferenceManager.getDefaultSharedPreferences(context).edit()
                .putInt(context.getString(R.string.settings_key_update_last_notified_version_code),
                        info.getVersionCode())
                .apply();
    }

    static Result parseReleaseList(@NonNull String json, boolean includePrereleases,
                                   int installedVersionCode) {
        JsonElement root = JsonParser.parseString(json);
        if (!root.isJsonArray()) {
            throw new JsonParseException("GitHub releases response is not an array");
        }
        JsonArray releases = root.getAsJsonArray();
        UpdateInfo newest = null;

        for (JsonElement releaseElement : releases) {
            if (!releaseElement.isJsonObject()) {
                continue;
            }
            JsonObject release = releaseElement.getAsJsonObject();
            if (getBoolean(release, "draft", false)) {
                continue;
            }

            boolean prerelease = getBoolean(release, "prerelease", false);
            if (prerelease && !includePrereleases) {
                continue;
            }

            JsonElement assetsElement = release.get("assets");
            if (assetsElement == null || !assetsElement.isJsonArray()) {
                continue;
            }
            JsonArray assets = assetsElement.getAsJsonArray();

            boolean hasApk = false;
            int releaseVersionCode = 0;
            for (JsonElement assetElement : assets) {
                if (!assetElement.isJsonObject()) {
                    continue;
                }
                String assetName = getString(assetElement.getAsJsonObject(), "name", "");
                if (!assetName.toLowerCase(java.util.Locale.ROOT).endsWith(".apk")) {
                    continue;
                }
                hasApk = true;
                releaseVersionCode = Math.max(
                        releaseVersionCode, extractVersionCode(assetName, APK_VERSION_CODE_PATTERN));
            }

            if (!hasApk) {
                continue;
            }
            if (releaseVersionCode == 0) {
                releaseVersionCode = extractVersionCode(
                        getString(release, "body", ""), BODY_VERSION_CODE_PATTERN);
            }
            if (releaseVersionCode <= installedVersionCode) {
                continue;
            }

            String htmlUrl = getString(release, "html_url", "");
            if (!htmlUrl.startsWith("https://github.com/")) {
                continue;
            }
            String title = getString(release, "name", "").trim();
            if (title.isEmpty()) {
                title = getString(release, "tag_name", "").trim();
            }
            if (title.isEmpty()) {
                title = "Wolvic update";
            }

            if (newest == null || releaseVersionCode > newest.getVersionCode()
                    || (releaseVersionCode == newest.getVersionCode()
                    && newest.isPrerelease() && !prerelease)) {
                newest = new UpdateInfo(releaseVersionCode, title, htmlUrl, prerelease);
            }
        }

        return newest == null
                ? new Result(Status.UP_TO_DATE, null)
                : new Result(Status.UPDATE_AVAILABLE, newest);
    }

    private static boolean getBoolean(@NonNull JsonObject object, @NonNull String key,
                                      boolean fallback) {
        JsonElement element = object.get(key);
        return element == null || element.isJsonNull() ? fallback : element.getAsBoolean();
    }

    @NonNull
    private static String getString(@NonNull JsonObject object, @NonNull String key,
                                    @NonNull String fallback) {
        JsonElement element = object.get(key);
        return element == null || element.isJsonNull() ? fallback : element.getAsString();
    }

    private static int extractVersionCode(@NonNull String value, @NonNull Pattern pattern) {
        Matcher matcher = pattern.matcher(value);
        int highest = 0;
        while (matcher.find()) {
            try {
                long candidate = Long.parseLong(matcher.group(1));
                if (candidate <= Integer.MAX_VALUE) {
                    highest = Math.max(highest, (int) candidate);
                }
            } catch (NumberFormatException ignored) {
                // Continue looking for another valid versionCode.
            }
        }
        return highest;
    }

    private static String readUtf8(@NonNull InputStream stream) throws IOException {
        StringBuilder body = new StringBuilder();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(stream, StandardCharsets.UTF_8))) {
            String line;
            while ((line = reader.readLine()) != null) {
                body.append(line).append('\n');
            }
        }
        return body.toString();
    }

    private static long getLastCheckTime(@NonNull Context context,
                                         @NonNull SharedPreferences prefs) {
        return prefs.getLong(
                context.getString(R.string.settings_key_update_last_check_time), 0);
    }

    private static void saveSuccessfulResult(@NonNull Context context,
                                             @NonNull SharedPreferences prefs,
                                             @NonNull Result result,
                                             long checkTime) {
        SharedPreferences.Editor editor = prefs.edit()
                .putLong(context.getString(R.string.settings_key_update_last_check_time), checkTime);
        UpdateInfo info = result.getUpdateInfo();
        if (result.getStatus() == Status.UPDATE_AVAILABLE && info != null) {
            editor.putInt(context.getString(R.string.settings_key_update_cached_version_code),
                            info.getVersionCode())
                    .putString(context.getString(R.string.settings_key_update_cached_title),
                            info.getTitle())
                    .putString(context.getString(R.string.settings_key_update_cached_url),
                            info.getHtmlUrl())
                    .putBoolean(context.getString(R.string.settings_key_update_cached_prerelease),
                            info.isPrerelease());
        } else if (result.getStatus() == Status.UP_TO_DATE) {
            editor.remove(context.getString(R.string.settings_key_update_cached_version_code))
                    .remove(context.getString(R.string.settings_key_update_cached_title))
                    .remove(context.getString(R.string.settings_key_update_cached_url))
                    .remove(context.getString(R.string.settings_key_update_cached_prerelease));
        }
        editor.apply();
    }

    private static void dispatch(@NonNull Context context, @Nullable Callback callback,
                                 @NonNull Result result) {
        if (callback == null) {
            return;
        }
        ((VRBrowserApplication) context.getApplicationContext()).getExecutors()
                .mainThread().execute(() -> callback.onResult(result));
    }
}
