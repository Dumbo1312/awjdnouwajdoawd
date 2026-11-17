package com.aioverlay.app

import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.recyclerview.widget.RecyclerView

class ChatAdapter(private val messages: List<ChatMessage>) :
    RecyclerView.Adapter<ChatAdapter.VH>() {

    companion object {
        private const val TYPE_USER = 0
        private const val TYPE_AI = 1
    }

    override fun getItemViewType(position: Int) =
        if (messages[position].isUser) TYPE_USER else TYPE_AI

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): VH {
        val layout = if (viewType == TYPE_USER) R.layout.item_message_user else R.layout.item_message_ai
        val view = LayoutInflater.from(parent.context).inflate(layout, parent, false)
        return VH(view)
    }

    override fun onBindViewHolder(holder: VH, position: Int) {
        holder.tvMessage.text = messages[position].text
    }

    override fun getItemCount() = messages.size

    class VH(view: View) : RecyclerView.ViewHolder(view) {
        val tvMessage: TextView = view.findViewById(R.id.tvMessage)
    }
}
