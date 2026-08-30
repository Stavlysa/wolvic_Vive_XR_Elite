/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package com.igalia.wolvic.ui.widgets.settings;

import android.content.Context;
import android.view.LayoutInflater;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.databinding.DataBindingUtil;

import com.igalia.wolvic.BuildConfig;
import com.igalia.wolvic.R;
import com.igalia.wolvic.browser.SettingsStore;
import com.igalia.wolvic.databinding.OptionsUpdateBinding;
import com.igalia.wolvic.ui.widgets.WidgetManagerDelegate;
import com.igalia.wolvic.utils.AppUpdateManager;

class UpdateOptionsView extends SettingsView {
    private OptionsUpdateBinding mBinding;
    private AppUpdateManager.UpdateInfo mAvailableUpdate;

    UpdateOptionsView(Context context, WidgetManagerDelegate widgetManager) {
        super(context, widgetManager);
        updateUI();
    }

    @Override
    protected void updateUI() {
        super.updateUI();

        LayoutInflater inflater = LayoutInflater.from(getContext());
        mBinding = DataBindingUtil.inflate(inflater, R.layout.options_update, this, true);
        mScrollbar = mBinding.scrollbar;
        mBinding.headerLayout.setBackClickListener(view -> onDismiss());

        SettingsStore settings = SettingsStore.getInstance(getContext());
        mBinding.automaticUpdateSwitch.setValue(settings.isUpdateCheckEnabled(), false);
        mBinding.automaticUpdateSwitch.setOnCheckedChangeListener(
                (button, enabled, apply) -> settings.setUpdateCheckEnabled(enabled));

        mBinding.includePrereleasesSwitch.setValue(
                settings.isUpdateIncludePrereleasesEnabled(), false);
        mBinding.includePrereleasesSwitch.setOnCheckedChangeListener(
                (button, enabled, apply) -> {
                    settings.setUpdateIncludePrereleasesEnabled(enabled);
                    mAvailableUpdate = null;
                    mBinding.availableUpdateButton.setVisibility(View.GONE);
                    mBinding.checkNowButton.setDescription(
                            getContext().getString(R.string.update_check_now_description));
                });

        mBinding.checkNowButton.setOnClickListener(view -> checkNow());
        mBinding.githubRepositoryButton.setOnClickListener(view -> {
            mWidgetManager.openNewTabForeground(AppUpdateManager.REPOSITORY_URL);
            exitWholeSettings();
        });
        mBinding.availableUpdateButton.setOnClickListener(view -> openAvailableUpdate());
        showAvailableUpdate(AppUpdateManager.getCachedUpdate(getContext()));
    }

    private void checkNow() {
        mBinding.checkNowButton.setEnabled(false);
        mBinding.checkNowButton.setDescription(
                getContext().getString(R.string.update_checking));
        AppUpdateManager.checkForUpdates(getContext(), true, result -> {
            if (!isAttachedToWindow()) {
                return;
            }

            mBinding.checkNowButton.setEnabled(true);
            switch (result.getStatus()) {
                case UPDATE_AVAILABLE:
                    showAvailableUpdate(result.getUpdateInfo());
                    break;
                case UP_TO_DATE:
                    showAvailableUpdate(null);
                    mBinding.checkNowButton.setDescription(getContext().getString(
                            R.string.update_up_to_date, BuildConfig.VERSION_CODE));
                    break;
                case ERROR:
                    mBinding.checkNowButton.setDescription(
                            getContext().getString(R.string.update_check_error));
                    break;
                case SKIPPED:
                    mBinding.checkNowButton.setDescription(
                            getContext().getString(R.string.update_check_now_description));
                    break;
            }
        });
    }

    private void showAvailableUpdate(AppUpdateManager.UpdateInfo info) {
        mAvailableUpdate = info;
        if (info == null) {
            mBinding.availableUpdateButton.setVisibility(View.GONE);
            return;
        }

        String channel = getContext().getString(info.isPrerelease()
                ? R.string.update_channel_release_prerelease
                : R.string.update_channel_release);
        mBinding.availableUpdateButton.setDescription(getContext().getString(
                R.string.update_available_description,
                info.getTitle(), info.getVersionCode(), channel));
        mBinding.availableUpdateButton.setVisibility(View.VISIBLE);
        mBinding.checkNowButton.setDescription(
                getContext().getString(R.string.update_available_title));
    }

    private void openAvailableUpdate() {
        if (mAvailableUpdate == null) {
            return;
        }
        AppUpdateManager.markNotified(getContext(), mAvailableUpdate);
        mWidgetManager.openNewTabForeground(mAvailableUpdate.getHtmlUrl());
        exitWholeSettings();
    }

    @NonNull
    @Override
    protected SettingViewType getType() {
        return SettingViewType.UPDATE;
    }
}
