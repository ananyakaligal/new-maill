import { createClient as createSupabaseClient } from "@supabase/supabase-js"
import type { Database } from "@/lib/database.types"

export function createClient() {
  const supabaseUrl = process.env.NEXT_PUBLIC_SUPABASE_FASTMAIL_URL
  const supabaseKey = process.env.SUPABASE_FASTMAIL_SERVICE_ROLE_KEY

  if (!supabaseUrl || !supabaseKey) {
    throw new Error("Missing Supabase environment variables")
  }

  return createSupabaseClient<Database>(supabaseUrl, supabaseKey)
}

