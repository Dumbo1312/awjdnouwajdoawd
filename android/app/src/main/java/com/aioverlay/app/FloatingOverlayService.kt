package com.aioverlay.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.graphics.PixelFormat
import android.os.IBinder
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.EditText
import android.widget.ImageButton
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.TextView
import androidx.core.app.NotificationCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class FloatingOverlayService : Service() {

    private lateinit var windowManager: WindowManager
    private var bubbleView: View? = null
    private var chatView: View? = null
    private var isChatOpen = false

    private val messages = mutableListOf<ChatMessage>()
    private lateinit var chatAdapter: ChatAdapter
    private lateinit var apiClient: ClaudeApiClient

    private val bubbleParams by lazy {
        WindowManager.LayoutParams(
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.WRAP_CONTENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            x = 0
            y = 200
        }
    }

    private val chatParams by lazy {
        WindowManager.LayoutParams(
            (resources.displayMetrics.widthPixels * 0.92).toInt(),
            (resources.displayMetrics.heightPixels * 0.60).toInt(),
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL
            y = 100
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        windowManager = getSystemService(WINDOW_SERVICE) as WindowManager
        apiClient = ClaudeApiClient(this)
        startForeground(NOTIF_ID, buildNotification())
        showBubble()
    }

    private fun showBubble() {
        val inflater = LayoutInflater.from(this)
        bubbleView = inflater.inflate(R.layout.overlay_bubble, null)

        val bubbleIcon = bubbleView!!.findViewById<ImageView>(R.id.ivBubble)

        // Drag logic
        var initialX = 0; var initialY = 0
        var initialTouchX = 0f; var initialTouchY = 0f
        var isDragging = false

        bubbleIcon.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> {
                    initialX = bubbleParams.x; initialY = bubbleParams.y
                    initialTouchX = event.rawX; initialTouchY = event.rawY
                    isDragging = false
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = (event.rawX - initialTouchX).toInt()
                    val dy = (event.rawY - initialTouchY).toInt()
                    if (Math.abs(dx) > 10 || Math.abs(dy) > 10) isDragging = true
                    if (isDragging) {
                        bubbleParams.x = initialX + dx
                        bubbleParams.y = initialY + dy
                        windowManager.updateViewLayout(bubbleView, bubbleParams)
                    }
                    true
                }
                MotionEvent.ACTION_UP -> {
                    if (!isDragging) toggleChat()
                    true
                }
                else -> false
            }
        }

        windowManager.addView(bubbleView, bubbleParams)
    }

    private fun toggleChat() {
        if (isChatOpen) {
            closeChat()
        } else {
            openChat()
        }
    }

    private fun openChat() {
        if (chatView != null) return
        isChatOpen = true

        val inflater = LayoutInflater.from(this)
        chatView = inflater.inflate(R.layout.overlay_chat, null)

        val rvMessages = chatView!!.findViewById<RecyclerView>(R.id.rvMessages)
        val etInput = chatView!!.findViewById<EditText>(R.id.etInput)
        val btnSend = chatView!!.findViewById<ImageButton>(R.id.btnSend)
        val btnClose = chatView!!.findViewById<ImageButton>(R.id.btnClose)
        val btnClear = chatView!!.findViewById<ImageButton>(R.id.btnClear)
        val progressBar = chatView!!.findViewById<ProgressBar>(R.id.progressBar)
        val tvStatus = chatView!!.findViewById<TextView>(R.id.tvStatus)

        chatAdapter = ChatAdapter(messages)
        rvMessages.apply {
            layoutManager = LinearLayoutManager(context).also { it.stackFromEnd = true }
            adapter = chatAdapter
        }

        btnClose.setOnClickListener { closeChat() }

        btnClear.setOnClickListener {
            messages.clear()
            chatAdapter.notifyDataSetChanged()
            apiClient.clearHistory()
        }

        btnSend.setOnClickListener {
            val text = etInput.text.toString().trim()
            if (text.isEmpty()) return@setOnClickListener
            etInput.text.clear()

            addMessage(ChatMessage(text, true))
            progressBar.visibility = View.VISIBLE
            tvStatus.text = "Thinking..."
            tvStatus.visibility = View.VISIBLE
            btnSend.isEnabled = false

            CoroutineScope(Dispatchers.IO).launch {
                val reply = apiClient.sendMessage(text)
                withContext(Dispatchers.Main) {
                    progressBar.visibility = View.GONE
                    tvStatus.visibility = View.GONE
                    btnSend.isEnabled = true
                    addMessage(ChatMessage(reply, false))
                    rvMessages.scrollToPosition(messages.size - 1)
                }
            }
        }

        // Scroll after messages load
        if (messages.isNotEmpty()) {
            rvMessages.post { rvMessages.scrollToPosition(messages.size - 1) }
        }

        windowManager.addView(chatView, chatParams)
    }

    private fun addMessage(msg: ChatMessage) {
        messages.add(msg)
        chatAdapter.notifyItemInserted(messages.size - 1)
        chatView?.findViewById<RecyclerView>(R.id.rvMessages)
            ?.scrollToPosition(messages.size - 1)
    }

    private fun closeChat() {
        isChatOpen = false
        chatView?.let {
            windowManager.removeView(it)
            chatView = null
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        bubbleView?.let { windowManager.removeView(it) }
        chatView?.let { windowManager.removeView(it) }
    }

    private fun buildNotification(): Notification {
        val channelId = "ai_overlay_channel"
        val channel = NotificationChannel(
            channelId, "AI Overlay", NotificationManager.IMPORTANCE_LOW
        )
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)

        val stopIntent = PendingIntent.getService(
            this, 0,
            Intent(this, FloatingOverlayService::class.java).setAction("STOP"),
            PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, channelId)
            .setContentTitle("AI Overlay active")
            .setContentText("Tap the bubble to chat • tap here to stop")
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentIntent(stopIntent)
            .build()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == "STOP") {
            stopSelf()
        }
        return START_STICKY
    }

    companion object {
        private const val NOTIF_ID = 1
    }
}
