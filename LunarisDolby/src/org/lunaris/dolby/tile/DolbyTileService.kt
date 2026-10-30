/*
 * Copyright (C) 2024-2025 Lunaris AOSP
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lunaris.dolby.tile

import android.content.res.Resources
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import org.lunaris.dolby.R
import org.lunaris.dolby.data.DolbyRepository
import org.lunaris.dolby.service.DolbyEffectService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.launch

class DolbyTileService : TileService() {

    private val repositoryDelegate = lazy { DolbyRepository(applicationContext) }
    private val repository by repositoryDelegate
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private var availabilityJob: Job? = null

    override fun onStartListening() {
        super.onStartListening()
        availabilityJob?.cancel()
        availabilityJob = scope.launch {
            repository.effectAvailable.collect { available -> if (available) updateTile() }
        }
        updateTile()
    }

    override fun onStopListening() {
        availabilityJob?.cancel()
        availabilityJob = null
        super.onStopListening()
    }

    override fun onClick() {
        super.onClick()
        val enabled = repository.getDolbyEnabled()
        repository.setDolbyEnabled(!enabled)
        if (enabled) DolbyEffectService.stop(this) else DolbyEffectService.start(this)
        updateTile()
    }

    private fun updateTile() {
        qsTile?.apply {
            try {
                repository.ensureReady()
            } catch (e: RuntimeException) {
                state = Tile.STATE_UNAVAILABLE
                subtitle = getString(R.string.dolby_unavailable)
                updateTile()
                return
            }
            val enabled = repository.getDolbyEnabled()
            state = if (enabled) Tile.STATE_ACTIVE else Tile.STATE_INACTIVE
            subtitle = getProfileName()
            updateTile()
        }
    }

    override fun onDestroy() {
        scope.cancel()
        if (repositoryDelegate.isInitialized()) repository.close()
        super.onDestroy()
    }

    private fun getProfileName(): String {
        val profile = repository.getCurrentProfile()
        val profiles = resources.getStringArray(R.array.dolby_profile_entries)
        val profileValues = resources.getStringArray(R.array.dolby_profile_values)
        
        return try {
            val index = profileValues.indexOf(profile.toString())
            if (index != -1) profiles[index] else getString(R.string.dolby_unknown)
        } catch (e: Exception) {
            getString(R.string.dolby_unknown)
        }
    }
}
