/*
 * Copyright (C) 2019 The Android Open Source Project
 *           (C) 2023-24 Paranoid Android
 *           (C) 2024-2025 Lunaris AOSP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lunaris.dolby.provider

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.net.Uri
import android.os.Bundle
import org.lunaris.dolby.R
import org.lunaris.dolby.DolbyConstants

private const val KEY_DOLBY = "dolby"
private const val META_DATA_PREFERENCE_SUMMARY = "com.android.settings.summary"

class SummaryProvider : ContentProvider() {

    override fun call(method: String, arg: String?, extras: Bundle?): Bundle? {
        val summary = when (method) {
            KEY_DOLBY -> getDolbySummary()
            else -> return null
        }
        return Bundle().apply {
            putString(META_DATA_PREFERENCE_SUMMARY, summary)
        }
    }

    override fun onCreate(): Boolean = true

    override fun query(
        uri: Uri,
        projection: Array<String>?,
        selection: String?,
        selectionArgs: Array<String>?,
        sortOrder: String?
    ): Cursor? = null

    override fun getType(uri: Uri): String? = null

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null

    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<String>?): Int = 0

    override fun update(
        uri: Uri,
        values: ContentValues?,
        selection: String?,
        selectionArgs: Array<String>?
    ): Int = 0

    private fun getDolbySummary(): String {
        val context = context ?: return ""
        // Settings reads the configured state without allocating an audio effect.
        val prefs = context.getSharedPreferences("dolby_prefs", android.content.Context.MODE_PRIVATE)
        if (!prefs.getBoolean(DolbyConstants.PREF_ENABLE,
                context.resources.getBoolean(R.bool.dolby_enabled_by_default))) {
            return context.getString(R.string.dolby_off)
        }
        val profiles = context.resources.getStringArray(R.array.dolby_profile_entries)
        val profileValues = context.resources.getStringArray(R.array.dolby_profile_values)
        val profile = prefs.getString(DolbyConstants.PREF_PROFILE, "0")
            ?.takeIf { it in profileValues } ?: "0"
        
        return try {
            val index = profileValues.indexOf(profile)
            val profileName = if (index != -1) profiles[index] else context.getString(R.string.dolby_unknown)
            context.getString(R.string.dolby_on_with_profile, profileName)
        } catch (e: Exception) {
            context.getString(R.string.dolby_on)
        }
    }
}
