/*
 * Copyright (C) 2011 The CyanogenMod Project
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

import android.util.Log;

import java.io.BufferedReader;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.FileReader;
import java.io.IOException;
import java.io.SyncFailedException;

public class Utils {
    private static final String TAG = "TenderloinParts_Utils";
    private static final String TAG_READ = "TenderloinParts_Utils_Read";
    private static final String TAG_WRITE = "TenderloinParts_Utils_Write";

    /**
     * Write a string value to the specified file.
     *
     * @param filename The filename
     * @param value The value
     */
    public static void writeValue(String filename, String value) {
        Process p = null;
        DataOutputStream dos = null;
        try {
             p = Runtime.getRuntime().exec("su");

            dos = new DataOutputStream(p.getOutputStream());

            dos.writeBytes("echo " + value + " > " + filename + "\n");
            dos.writeBytes("exit\n");

        } catch (FileNotFoundException ex) {
            Log.w(TAG, "file " + filename + " not found: " + ex);
        } catch (SyncFailedException ex) {
            Log.w(TAG, "file " + filename + " sync failed: " + ex);
        } catch (IOException ex) {
            Log.w(TAG, "IOException trying to sync " + filename + ": " + ex);
        } catch (RuntimeException ex) {
            Log.w(TAG, "exception while syncing file: ", ex);
        } finally {
            if (dos != null) {
                try {
                    Log.w(TAG_WRITE, "file " + filename + ": " + value);
                    dos.flush();
                    dos.close();
                    if (p.waitFor() != 0)
                        Log.e(TAG, "Error writing to file: " + filename);
                } catch (InterruptedException ex) {
                    Log.w(TAG, "exception while waiting for process to close", ex);
                } catch (IOException ex) {
                    Log.w(TAG, "IOException while closing synced file: ", ex);
                } catch (RuntimeException ex) {
                    Log.w(TAG, "exception while closing file: ", ex);
                }
            }
        }

    }

    /**
     * Write a string value to the specified file.
     *
     * @param filename The filename
     * @param value The value
     */
    public static void writeValue(String filename, Boolean value) {
        writeValue(filename, value ? "1" : "0");
    }

    /**
     * Write the "color value" to the specified file. The value is scaled from
     * an integer to an unsigned integer by multiplying by 2.
     *
     * @param filename The filename
     * @param value The value of max value Integer.MAX
     */
    public static void writeColor(String filename, int value) {
        writeValue(filename, String.valueOf((long) value * 2));
    }

    /**
     * Check if the specified file exists.
     *
     * @param filename The filename
     * @return Whether the file exists or not
     */
    public static boolean fileExists(String filename) {
        return new File(filename).exists();
    }

    // Read value from sysfs interface
    public static String readOneLine(String sFile) {
        BufferedReader brBuffer;
        String sLine = null;

        try {
            brBuffer = new BufferedReader(new FileReader(sFile), 512);
            try {
                sLine = brBuffer.readLine();
            } finally {
                brBuffer.close();
            }
        } catch (Exception e) {
            Log.e(TAG_READ, "IO Exception when reading /sys/ file", e);
        }
        return sLine;
    }
}
