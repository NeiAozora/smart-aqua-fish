<?php

namespace App\Http\Controllers;

use Illuminate\Http\Request;
use Illuminate\Support\Facades\DB;
use Illuminate\Support\Facades\Hash;
use Illuminate\Support\Str;

class AuthController extends Controller
{
    public function login(Request $request)
    {
        $request->validate([
            'username' => 'required|string',
            'password' => 'required|string'
        ]);
        
        $user = DB::table('users')->where('username', $request->username)->first();
        
        if (!$user || !Hash::check($request->password, $user->password_hash)) {
            return response()->json(['message' => 'Invalid credentials'], 401);
        }
        
        $token = Str::random(80);
        $expiredAt = now()->addDays(7);
        
        DB::table('users')
            ->where('id_user', $user->id_user)
            ->update([
                'session_token' => $token,
                'session_expired_at' => $expiredAt
            ]);
        
        return response()->json([
            'token' => $token,
            'expired_at' => $expiredAt,
            'user' => [
                'id' => $user->id_user,
                'name' => $user->name,
                'username' => $user->username
            ]
        ]);
    }
    
    public function logout(Request $request)
    {
        $user = $request->user;
        DB::table('users')
            ->where('id_user', $user->id_user)
            ->update([
                'session_token' => null,
                'session_expired_at' => null
            ]);
        
        return response()->json(['message' => 'Logged out']);
    }
    
    public function me(Request $request)
    {
        $user = $request->user;
        return response()->json([
            'id' => $user->id_user,
            'name' => $user->name,
            'username' => $user->username
        ]);
    }


    public function updateProfile(Request $request)
    {
        $user = $request->user; // dari middleware
        
        $request->validate([
            'name' => 'sometimes|string|max:65',
            'username' => 'sometimes|string|max:50|unique:users,username,' . $user->id_user . ',id_user',
            'password' => 'sometimes|string|min:4'
        ]);
        
        $updateData = [];
        if ($request->has('name')) $updateData['name'] = $request->name;
        if ($request->has('username')) $updateData['username'] = $request->username;
        if ($request->has('password')) $updateData['password_hash'] = Hash::make($request->password);
        
        if (empty($updateData)) {
            return response()->json(['message' => 'Tidak ada data yang diubah'], 200);
        }
        
        DB::table('users')->where('id_user', $user->id_user)->update($updateData);
        
        return response()->json(['message' => 'Profil berhasil diupdate']);
    }
}
