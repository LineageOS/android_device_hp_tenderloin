/*
 * Copyright (C) 2012 Tomasz Rostanski
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.cyanogenmod.settings.device;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.preference.CheckBoxPreference;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceActivity;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.util.Log;

import com.cyanogenmod.settings.device.R;

public class CameraFragmentActivity extends PreferenceFragment {

    private static final String TAG = "TenderloinParts_Camera";
    private static final String CAMERA_CONF_BIN = "/system/bin/cam_config";
    private SharedPreferences mSharedPrefs;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        //Since camera mode could be changed without this application we will
        //make sure there is no discrepancy between current mode of driver and shared preferences
        mSharedPrefs = PreferenceManager.getDefaultSharedPreferences(getActivity());

        addPreferencesFromResource(R.xml.camera_preferences);
        setPreviewModePrefTitle(null);
        setRotationModePrefTitle(null);
        syncPreferences();

        Preference previewModePref = findPreference("preview_mode_preference");
        previewModePref.setOnPreferenceChangeListener(new OnPreferenceChangeListener() {
            @Override
            public boolean onPreferenceChange(Preference preference, Object newValue) {
                if (setPreviewMode((String)newValue)) {
                    setPreviewModePrefTitle((String)newValue);
                    return true;
                } else {
                    return false;
                }
            }
        });
        Preference rotationModePref = findPreference("rotation_mode_preference");
        rotationModePref.setOnPreferenceChangeListener(new OnPreferenceChangeListener() {
            @Override
            public boolean onPreferenceChange(Preference preference, Object newValue) {
                if (setRotationMode((String)newValue)) {
                    setRotationModePrefTitle((String)newValue);
                    return true;
                } else {
                    return false;
                }
            }
        });
    }

    private void setPreviewModePrefTitle(String mode) {
        Preference previewModePref = findPreference("preview_mode_preference");

        if (mode == null) {
            //Set title based on stored preference
            mode = mSharedPrefs.getString("preview_mode_preference", "");
        }

        if (previewModePref != null) {
            if (mode.equalsIgnoreCase("0")) {
                previewModePref.setTitle(getString(R.string.preview_mode_preference_title_mirrored));
                previewModePref.setSummary(getString(R.string.preview_mode_preference_summary_mirrored));
            } else if (mode.equalsIgnoreCase("1")) {
                previewModePref.setTitle(getString(R.string.preview_mode_preference_title_normal));
                previewModePref.setSummary(getString(R.string.preview_mode_preference_summary_normal));
            } else if (mode.equalsIgnoreCase("2")) {
                previewModePref.setTitle(getString(R.string.preview_mode_preference_title_rear));
                previewModePref.setSummary(getString(R.string.preview_mode_preference_summary_rear));
            }
        }
    }

    private void setRotationModePrefTitle(String mode) {
        Preference rotationModePref = findPreference("rotation_mode_preference");

        if (mode == null) {
            //Set title based on stored preference
            mode = mSharedPrefs.getString("rotation_mode_preference", "0");
        }

        if (rotationModePref != null) {
            rotationModePref.setTitle(mode);
            rotationModePref.setSummary(mode);
        }
    }

    private void syncPreferences() {
        String previewMode = getCurrentPreviewMode();
        SharedPreferences.Editor editor = mSharedPrefs.edit();
        if (previewMode != null) {
            String [] values = getResources().getStringArray(R.array.preview_mode_values);
            if (previewMode.equals("0"))
                editor.putString("preview_mode_preference", values[0]);
            else if (previewMode.equals("1"))
                editor.putString("preview_mode_preference", values[1]);
            else if (previewMode.equals("2"))
                editor.putString("preview_mode_preference", values[2]);
        }
        editor.putString("rotation_mode_preference", getCurrentRotationMode());
        editor.commit();
    }

    private String getCurrentPreviewMode() {
        String ret = null;
        Process process;
        try {
            process = Runtime.getRuntime().exec(new String[] { "su", "-c", CAMERA_CONF_BIN + " get preview"});
            DataInputStream is = new DataInputStream(process.getInputStream());
            process.waitFor();

            if (process.exitValue() == 0)
                ret = is.readLine();
            else
                Log.e(TAG, "Unable to get preview mode");
        } catch (Exception e) {
            e.printStackTrace();
        }
        return ret;
    }

    private boolean setPreviewMode(String mode) {
        int ret = -1;
        Process process;
        try {
            process = Runtime.getRuntime().exec(new String[] { "su", "-c", CAMERA_CONF_BIN + " set preview " + mode });
            process.waitFor();
            ret = process.exitValue();
        } catch (Exception e) {
            e.printStackTrace();
        }
        if (ret == 0)
            return true;
        else {
            Log.e(TAG, "Unable to set preview mode, ret=" + ret);
            return false;
        }
    }

    private String getCurrentRotationMode() {
        String ret = null;
        Process process;
        try {
            process = Runtime.getRuntime().exec(new String[] { "su", "-c", CAMERA_CONF_BIN + " get rotation"});
            DataInputStream is = new DataInputStream(process.getInputStream());
            process.waitFor();

            if (process.exitValue() == 0)
                ret = is.readLine();
            else
                Log.e(TAG, "Unable to get rotation");
        } catch (Exception e) {
            e.printStackTrace();
        }
        return ret;
    }

    private boolean setRotationMode(String mode) {
        int ret = -1;
        Process process;
        try {
            process = Runtime.getRuntime().exec(new String[] { "su", "-c", CAMERA_CONF_BIN + " set rotation " + mode});
            process.waitFor();
            ret = process.exitValue();
        } catch (Exception e) {
            e.printStackTrace();
        }
        if (ret == 0)
            return true;
        else {
            Log.e(TAG, "Unable to set rotation, ret=" + ret);
            return false;
        }
    }
}
