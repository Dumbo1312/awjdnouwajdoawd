package com.aioverlay.app

import android.content.Context
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.TimeUnit

class ClaudeApiClient(private val context: Context) {

    private val client = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(60, TimeUnit.SECONDS)
        .build()

    private val conversationHistory = JSONArray()

    fun clearHistory() {
        while (conversationHistory.length() > 0) {
            conversationHistory.remove(0)
        }
    }

    fun sendMessage(userText: String): String {
        val apiKey = context.getSharedPreferences("ai_overlay", Context.MODE_PRIVATE)
            .getString("api_key", "") ?: ""

        if (apiKey.isEmpty()) {
            return "No API key set. Open the AI Overlay app and paste your Claude API key."
        }

        // Add user message to history
        conversationHistory.put(JSONObject().apply {
            put("role", "user")
            put("content", userText)
        })

        val body = JSONObject().apply {
            put("model", "claude-sonnet-4-6")
            put("max_tokens", 1024)
            put("system", "You are a helpful AI assistant in a compact overlay. Keep answers concise and clear.")
            put("messages", conversationHistory)
        }

        return try {
            val request = Request.Builder()
                .url("https://api.anthropic.com/v1/messages")
                .addHeader("x-api-key", apiKey)
                .addHeader("anthropic-version", "2023-06-01")
                .addHeader("content-type", "application/json")
                .post(body.toString().toRequestBody("application/json".toMediaType()))
                .build()

            val response = client.newCall(request).execute()
            val responseBody = response.body?.string() ?: ""

            if (!response.isSuccessful) {
                val errJson = runCatching { JSONObject(responseBody) }.getOrNull()
                val errMsg = errJson?.optJSONObject("error")?.optString("message") ?: responseBody
                // Remove the user message we just added so history stays clean
                if (conversationHistory.length() > 0) conversationHistory.remove(conversationHistory.length() - 1)
                return "Error ${response.code}: $errMsg"
            }

            val json = JSONObject(responseBody)
            val assistantText = json
                .getJSONArray("content")
                .getJSONObject(0)
                .getString("text")

            // Add assistant reply to history
            conversationHistory.put(JSONObject().apply {
                put("role", "assistant")
                put("content", assistantText)
            })

            // Keep history bounded to last 20 messages (10 exchanges)
            while (conversationHistory.length() > 20) {
                conversationHistory.remove(0)
            }

            assistantText
        } catch (e: Exception) {
            if (conversationHistory.length() > 0) conversationHistory.remove(conversationHistory.length() - 1)
            "Network error: ${e.message}"
        }
    }
}
