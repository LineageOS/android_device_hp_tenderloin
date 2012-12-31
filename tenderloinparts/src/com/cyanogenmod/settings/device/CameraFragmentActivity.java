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

    private static final int[] MODE_TITLES = new int[] {
        R.string.preview_mode_preference_title_mirrored,
        R.string.preview_mode_preference_title_normal,
        R.string.preview_mode_preference_title_rear
    };
    private static final int[] MODE_SUMMARIES = new int[] {
        R.string.preview_mode_preference_summary_mirrored,
        R.string.preview_mode_preference_summary_normal,
        R.string.preview_mode_preference_summary_rear
    };
    private static final String TAG = "TenderloinParts_Camera";
    private static final String CAMERA_CONF_FILE = "/data/misc/camera/config.txt";
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
                String rotationMode = mSharedPrefs.getString("rotation_mode_preference", "");
                if (savePreferences((String)newValue, rotationMode)) {
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
                String previewMode = mSharedPrefs.getString("preview_mode_preference", "");
                if (savePreferences(previewMode, (String)newValue)) {
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
            int modeIndex = 0;
            if (!mode.isEmpty()) {
                modeIndex = Integer.parseInt(mode);
            }
            previewModePref.setTitle(getString(MODE_TITLES[modeIndex]));
            previewModePref.setSummary(getString(MODE_SUMMARIES[modeIndex]));
        }
    }

    private void setRotationModePrefTitle(String mode) {
        Preference rotationModePref = findPreference("rotation_mode_preference");

        if (mode == null) {
            //Set title based on stored preference
            mode = mSharedPrefs.getString("rotation_mode_preference", "0");
        }

        if (rotationModePref != null) {
            if (mode.isEmpty()) {
                mode = "0";
            }
            rotationModePref.setTitle(mode);
            rotationModePref.setSummary(mode);
        }
    }

    private void syncPreferences() {
        try {
            String command = "cat " + CAMERA_CONF_FILE + "\n";
            Process process = Runtime.getRuntime().exec(new String[] { "su", "-c", "system/bin/sh" });
            DataInputStream is = new DataInputStream(process.getInputStream());
            DataOutputStream ds = new DataOutputStream(process.getOutputStream());
            ds.writeBytes(command);
            ds.writeBytes("exit\n");
            process.waitFor();

            if (process.exitValue() == 0) {
                SharedPreferences.Editor editor = mSharedPrefs.edit();
                String s;

                while((s = is.readLine()) != null) {
                    if (s.startsWith("preview_mode")) {
                        int index = 0;
                        if (!s.substring(13).isEmpty()) {
                            index = Integer.parseInt(s.substring(13));
                        }
                        String [] values = getResources().getStringArray(R.array.preview_mode_values);
                        editor.putString("preview_mode_preference", values[index]);
                    } else if (s.startsWith("rotation_mode")) {
                        if (!s.substring(14).isEmpty()) {
                            editor.putString("rotation_mode_preference", s.substring(14));
                        } else {
                            editor.putString("rotation_mode_preference", "0");
                        }
                    }
                }

                editor.commit();
            }

            is.close();
            ds.close();
        } catch (Exception e) {
            Log.e(TAG, "Unable to read " + CAMERA_CONF_FILE + ", exception: " + e.toString());
        }
    }

    private boolean savePreferences(String previewMode, String rotationMode) {
        try {
            String command  = "echo \"";
            command += "preview_mode=" + previewMode + "\n";
            command += "rotation_mode=" + rotationMode;
            command += "\" > " + CAMERA_CONF_FILE + "\n";

            Process process = Runtime.getRuntime().exec(new String[] { "su", "-c", "system/bin/sh" });
            DataOutputStream ds = new DataOutputStream(process.getOutputStream());
            ds.writeBytes(command);
            ds.writeBytes("exit\n");
            process.waitFor();

            ds.close();

            if (process.exitValue() == 0) {
                return true;
            }
        } catch (Exception e) {
            Log.e(TAG, "Unable to write " + CAMERA_CONF_FILE + ", exception: " + e.toString());
        }
        return false;
    }
}
