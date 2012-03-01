/*
 * Copyright (C) 2012 The CyanogenMod Project
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
import java.util.Arrays;

import com.cyanogenmod.settings.device.R;

import android.content.Context;
import android.content.SharedPreferences;
import android.os.Bundle;
import android.os.Handler;
import android.preference.CheckBoxPreference;
import android.preference.ListPreference;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceChangeListener;
import android.preference.PreferenceActivity;
import android.preference.PreferenceFragment;
import android.preference.PreferenceManager;
import android.preference.PreferenceScreen;
import android.util.Log;

public class ChargerControlFragmentActivity extends PreferenceFragment {

    private static final String TAG = "TenderloinParts_ChargerControl";
    private static final String CURRENT_SYSFS = "/sys/power/charger/currentlimit";
    private Preference mChargerCurrentDisplay;
    private ListPreference mChargerLimitPref;

    private Handler handler = new Handler();
    final Runnable mGetCurrentRunnable = new Runnable()
    {
        public void run() 
        {
            String [] validValues = getResources().getStringArray(R.array.charger_control_limits);
            String title = getChargerCurrent();

            if (title != null) {
                //Set display for current
                if (title.contains("none"))
                    mChargerCurrentDisplay.setTitle("No charger detected");
                else
                    mChargerCurrentDisplay.setTitle( getString(R.string.charger_control_display_prefix) 
                                                        + " " + title);                

                //Set limit preference to current value if valid
                if (Arrays.asList(validValues).contains(title))
                    mChargerLimitPref.setValue(title);

            } else {
                mChargerCurrentDisplay.setTitle("Error while checking charger status!");
            }
            handler.postDelayed(this, 1000);
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (!Utils.fileExists(CURRENT_SYSFS))
            return;

        addPreferencesFromResource(R.xml.chargercontrol_preference);
        mChargerCurrentDisplay = findPreference("charger_current_display");
        mChargerLimitPref = (ListPreference) findPreference("charger_control_limit_pref");

        mChargerLimitPref.setTitle(R.string.charger_control_preference_title);
        mChargerLimitPref.setSummary(R.string.charger_control_preference_summary);
        mChargerLimitPref.setPersistent(false);

        mChargerLimitPref.setOnPreferenceChangeListener(new OnPreferenceChangeListener() {
            @Override
            public boolean onPreferenceChange(Preference preference, Object newValue) {
                    setChargerLimit((String)newValue);
                return true;
            }
        });
    }

    @Override
    public void onStart() {
        super.onStart();
        handler.postDelayed(mGetCurrentRunnable, 0);
    }

    @Override
    public void onStop() {
        super.onStop();
        handler.removeCallbacks(mGetCurrentRunnable);
    }

    private String getChargerCurrent() {
        String current =  Utils.readOneLine(CURRENT_SYSFS);
        if (current != null) {
            //Sysfs returns something like "current500ma"
            //this will remove the word "current" so we are 
            //left with only the value + units
            current = current.replace("current", "");
        } else
            Log.e(TAG, "Unable to poll get current charger limit!");
        return current;
    }

    private void setChargerLimit(String value) {
        Utils.writeValue(CURRENT_SYSFS, "current" + value);
    }
}