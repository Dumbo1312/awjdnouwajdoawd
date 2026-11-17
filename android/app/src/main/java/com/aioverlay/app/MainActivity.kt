package com.aioverlay.app

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.aioverlay.app.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

    private lateinit var binding: ActivityMainBinding

    private val overlayPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.StartActivityForResult()
    ) {
        if (Settings.canDrawOverlays(this)) {
            startOverlayService()
        } else {
            Toast.makeText(this, "Overlay permission required", Toast.LENGTH_LONG).show()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        binding = ActivityMainBinding.inflate(layoutInflater)
        setContentView(binding.root)

        binding.btnStartOverlay.setOnClickListener {
            checkAndStartOverlay()
        }

        binding.btnStopOverlay.setOnClickListener {
            stopService(Intent(this, FloatingOverlayService::class.java))
            Toast.makeText(this, "Overlay stopped", Toast.LENGTH_SHORT).show()
        }

        binding.btnSaveKey.setOnClickListener {
            val key = binding.etApiKey.text.toString().trim()
            if (key.isNotEmpty()) {
                getSharedPreferences("ai_overlay", MODE_PRIVATE)
                    .edit().putString("api_key", key).apply()
                Toast.makeText(this, "API key saved", Toast.LENGTH_SHORT).show()
            } else {
                Toast.makeText(this, "Enter your Claude API key first", Toast.LENGTH_SHORT).show()
            }
        }

        // Pre-fill saved key (masked)
        val savedKey = getSharedPreferences("ai_overlay", MODE_PRIVATE)
            .getString("api_key", "") ?: ""
        if (savedKey.isNotEmpty()) {
            binding.etApiKey.hint = "Key saved (${savedKey.take(8)}...)"
        }
    }

    private fun checkAndStartOverlay() {
        if (!Settings.canDrawOverlays(this)) {
            val intent = Intent(
                Settings.ACTION_MANAGE_OVERLAY_PERMISSION,
                Uri.parse("package:$packageName")
            )
            overlayPermissionLauncher.launch(intent)
        } else {
            startOverlayService()
        }
    }

    private fun startOverlayService() {
        startForegroundService(Intent(this, FloatingOverlayService::class.java))
        Toast.makeText(this, "Overlay started — tap the bubble!", Toast.LENGTH_SHORT).show()
        moveTaskToBack(true)
    }
}
