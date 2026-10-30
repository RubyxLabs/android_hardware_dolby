/*
 * Copyright (C) 2024-2025 Lunaris AOSP
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lunaris.dolby.service

import android.content.Intent
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import android.provider.Settings
import org.lunaris.dolby.DolbyConstants
import org.lunaris.dolby.data.AppProfileManager
import org.lunaris.dolby.data.DolbyRepository

class DolbyNotificationListener : NotificationListenerService() {

    private lateinit var appProfileManager: AppProfileManager
    private lateinit var dolbyRepository: DolbyRepository
    private var lastActivePackage: String? = null

    override fun onCreate() {
        super.onCreate()
        DolbyConstants.dlog(TAG, "NotificationListener created")
        appProfileManager = AppProfileManager(this)
        dolbyRepository = DolbyRepository(this)
        initializeDolbySettings()
        startAppProfileMonitoringIfEnabled()
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        DolbyConstants.dlog(TAG, "NotificationListener connected")
        initializeDolbySettings()
        startAppProfileMonitoringIfEnabled()
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        DolbyConstants.dlog(TAG, "NotificationListener disconnected")
        val component = android.content.ComponentName(this, DolbyNotificationListener::class.java)
        val enabled = Settings.Secure.getString(contentResolver, "enabled_notification_listeners")
            .orEmpty().split(':').contains(component.flattenToString())
        if (enabled) {
            try {
                requestRebind(component)
            } catch (e: RuntimeException) {
                Log.w(TAG, "Notification listener could not reconnect", e)
            }
        }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        super.onNotificationPosted(sbn)
        sbn?.packageName?.let { packageName ->
            if (packageName != lastActivePackage && packageName != this.packageName) {
                lastActivePackage = packageName
                handlePackageChange(packageName)
            }
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        super.onNotificationRemoved(sbn)
    }

    private fun initializeDolbySettings() {
        try {
            val prefs = getSharedPreferences("dolby_prefs", MODE_PRIVATE)
            val savedProfile = prefs.getString(DolbyConstants.PREF_PROFILE, "0")?.toIntOrNull() ?: 0
            val enabled = prefs.getBoolean(DolbyConstants.PREF_ENABLE,
                resources.getBoolean(org.lunaris.dolby.R.bool.dolby_enabled_by_default))
            DolbyConstants.dlog(TAG, "Initializing Dolby - enabled: $enabled, profile: $savedProfile")
            if (enabled) {
                dolbyRepository.setCurrentProfile(savedProfile)
                dolbyRepository.setDolbyEnabled(true)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed to initialize Dolby settings", e)
        }
    }

    private fun startAppProfileMonitoringIfEnabled() {
        val prefs = getSharedPreferences("dolby_prefs", MODE_PRIVATE)
        val isEnabled = prefs.getBoolean("app_profile_monitoring_enabled", false)
        if (isEnabled) {
            DolbyConstants.dlog(TAG, "Starting app profile monitoring")
            AppProfileMonitorService.startMonitoring(this)
        }
    }

    private fun handlePackageChange(packageName: String) {
        val prefs = getSharedPreferences("dolby_prefs", MODE_PRIVATE)
        val isMonitoringEnabled = prefs.getBoolean("app_profile_monitoring_enabled", false)
        if (!isMonitoringEnabled) return
        try {
            val assignedProfile = appProfileManager.getAppProfile(packageName)
            if (assignedProfile >= 0) {
                DolbyConstants.dlog(TAG, "Package change detected: $packageName -> profile $assignedProfile")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error handling package change", e)
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        if (::dolbyRepository.isInitialized) dolbyRepository.close()
        DolbyConstants.dlog(TAG, "NotificationListener destroyed")
    }

    companion object {
        private const val TAG = "DolbyNotificationListener"
    }
}
