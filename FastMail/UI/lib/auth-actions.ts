"use server"

import { cookies } from "next/headers"
import { redirect } from "next/navigation"
import { createClient } from "@/lib/supabase-server"
import bcrypt from 'bcryptjs';
import type { User } from "@/lib/types"

export async function loginUser(email: string, password: string) {
  try {
    const supabase = createClient()

    const { data: user, error: userError } = await supabase
      .from("users")
      .select("*")
      .eq("email", email)
      .single()

    if (userError || !user) {
      return { success: false, error: "Invalid credentials" }
    }

    const passwordMatch = await bcrypt.compare(password, user.password_hash)
    if (!passwordMatch) {
      return { success: false, error: "Invalid credentials" }
    }

    const sessionId = crypto.randomUUID()
    const expiresAt = new Date(Date.now() + 7 * 24 * 60 * 60 * 1000) // 7 days

    const { error: sessionError } = await supabase.from("sessions").insert({
      id: sessionId,
      user_id: user.id,
      expires_at: expiresAt.toISOString(),
    })

    if (sessionError) {
      return { success: false, error: "Failed to create session" }
    }

    const cookieStore = await cookies()
    cookieStore.set("session_id", sessionId, {
      httpOnly: true,
      secure: process.env.NODE_ENV === "production",
      expires: expiresAt,
    })

    return { success: true }
  } catch (error) {
    console.error("Login error:", error)
    return { success: false, error: "An unexpected error occurred" }
  }
}

export async function registerUser(email: string, username: string, password: string) {
  try {
    const supabase = createClient()

    const { data: existingUser } = await supabase
      .from("users")
      .select("id")
      .or(`email.eq.${email},username.eq.${username}`)
      .maybeSingle()

    if (existingUser) {
      return { success: false, error: "Email or username already exists" }
    }

    const passwordHash = await bcrypt.hash(password, 10)

    const { error: createError } = await supabase
      .from("users")
      .insert({
        email,
        username,
        password_hash: passwordHash,
        created_at: new Date().toISOString(),
      })

    if (createError) {
      return { success: false, error: "Failed to create user" }
    }

    return { success: true }
  } catch (error) {
    console.error("Registration error:", error)
    return { success: false, error: "An unexpected error occurred" }
  }
}

export async function logoutUser() {
  try {
    const cookieStore = await cookies()
    const sessionId = cookieStore.get("session_id")?.value

    if (sessionId) {
      const supabase = createClient()
      await supabase.from("sessions").delete().eq("id", sessionId)
      cookieStore.delete("session_id")
    }

    return { success: true }
  } catch (error) {
    console.error("Logout error:", error)
    return { success: false, error: "An unexpected error occurred" }
  }
}

export async function getUserProfile() {
  try {
    const cookieStore = await cookies()
    const sessionId = cookieStore.get("session_id")?.value

    if (!sessionId) {
      return { success: false, error: "Not authenticated" }
    }

    const supabase = createClient()

    const { data: session, error: sessionError } = await supabase
      .from("sessions")
      .select("user_id, expires_at")
      .eq("id", sessionId)
      .single()

    if (sessionError || !session) {
      const cookieStore = await cookies()
      cookieStore.delete("session_id")
      return { success: false, error: "Invalid session" }
    }

    if (new Date(session.expires_at) < new Date()) {
      await supabase.from("sessions").delete().eq("id", sessionId)
      const cookieStore = await cookies()
      cookieStore.delete("session_id")
      return { success: false, error: "Session expired" }
    }

    const { data: user, error: userError } = await supabase
      .from("users")
      .select("id, email, username, full_name")
      .eq("id", session.user_id)
      .single()

    if (userError || !user) {
      return { success: false, error: "User not found" }
    }

    const userData: User = {
      id: user.id,
      email: user.email,
      username: user.username,
      fullName: user.full_name || "",
    }

    return { success: true, user: userData }
  } catch (error) {
    console.error("Get user profile error:", error)
    return { success: false, error: "An unexpected error occurred" }
  }
}

export async function updateUserProfile(data: {
  username?: string
  fullName?: string
}) {
  try {
    const cookieStore = await cookies()
    const sessionId = cookieStore.get("session_id")?.value

    if (!sessionId) {
      return { success: false, error: "Not authenticated" }
    }

    const supabase = createClient()

    const { data: session, error: sessionError } = await supabase
      .from("sessions")
      .select("user_id")
      .eq("id", sessionId)
      .single()

    if (sessionError || !session) {
      return { success: false, error: "Invalid session" }
    }

    const { error: updateError } = await supabase
      .from("users")
      .update({
        username: data.username,
        full_name: data.fullName,
        updated_at: new Date().toISOString(),
      })
      .eq("id", session.user_id)

    if (updateError) {
      return { success: false, error: "Failed to update profile" }
    }

    return { success: true }
  } catch (error) {
    console.error("Update user profile error:", error)
    return { success: false, error: "An unexpected error occurred" }
  }
}

export async function requireAuth() {
  const cookieStore = await cookies()
  const sessionId = cookieStore.get("session_id")?.value

  if (!sessionId) {
    redirect("/login")
  }

  const supabase = createClient()

  const { data: session, error: sessionError } = await supabase
    .from("sessions")
    .select("user_id, expires_at")
    .eq("id", sessionId)
    .single()

  if (sessionError || !session) {
    const cookieStore = await cookies()
    cookieStore.delete("session_id")
    redirect("/login")
  }

  if (new Date(session.expires_at) < new Date()) {
    await supabase.from("sessions").delete().eq("id", sessionId)
    const cookieStore = await cookies()
    cookieStore.delete("session_id")
    redirect("/login")
  }

  return session.user_id
}
