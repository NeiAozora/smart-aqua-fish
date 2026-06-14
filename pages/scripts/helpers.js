// scripts/helpers.js
// Helper untuk request HTTP yang resilient (deteksi HTML response)

// Fungsi redirect ke login (digunakan bersama)
function redirectToLogin() {
  const currentUrl = window.location.href;
  const urlObj = new URL(currentUrl);
  let pathname = urlObj.pathname;
  
  if (pathname !== '/' && pathname !== '') {
    const segments = pathname.split('/').filter(s => s !== '');
    if (segments.length > 0) {
      const lastSegment = segments[segments.length - 1];
      const lastSegmentIndex = pathname.lastIndexOf(lastSegment);
      pathname = pathname.substring(0, lastSegmentIndex);
    }
  }
  
  let loginUrl = `${urlObj.protocol}//${urlObj.host}${pathname}`;
  if (!loginUrl.endsWith('/')) {
    loginUrl += '/';
  }
  loginUrl += 'login.html';
  
  window.location.href = loginUrl;
}

async function makeRequest(url, options = {}) {
  try {
    const response = await fetch(url, options);

    // 🔐 Handle Unauthorized (401)
    if (response.status === 401) {
      console.warn('Unauthorized, redirecting to login...');
      redirectToLogin();
      return null;
    }

    // Baca response sebagai teks terlebih dahulu
    const text = await response.text();

    // Deteksi apakah response berupa HTML
    const isHTML = /^\s*<!DOCTYPE\s+html|<html/i.test(text) ||
                   /<(!DOCTYPE|html|head|body|div|span|p|a|script|style|meta|link|title)/i.test(text.slice(0, 1000));

    if (isHTML) {
      // Tampilkan halaman HTML (misal halaman error dari Laravel)
      document.open();
      document.write(text);
      document.close();
      return null;
    }

    // Jika bukan HTML, parse sebagai JSON
    return JSON.parse(text);
  } catch (err) {
    console.error('Request error:', err);
    
    // Fallback jika error message mengandung indikasi HTML
    if (err.message && isHtmlFileProcedure(err.message)) {
      return null;
    }
    
    // Lempar error agar bisa ditangani oleh pemanggil
    throw err;
  }
}

// Fungsi pendeteksi HTML dari string (untuk fallback)
function isHtmlFileProcedure(responseText) {
  const trimmed = responseText.trim();
  const isHTML = trimmed.startsWith('<!DOCTYPE') ||
                 trimmed.startsWith('<html') ||
                 /<(!DOCTYPE|html|head|body|div|span|p|a)/i.test(trimmed.substring(0, 500));
  if (isHTML) {
    document.open();
    document.write(responseText);
    document.close();
    return true;
  }
  return false;
}

// Helper khusus API dengan token
async function makeApiRequest(endpoint, options = {}) {
  const token = localStorage.getItem('token');
  const headers = {
    'Content-Type': 'application/json',
    ...options.headers
  };
  if (token) {
    headers['Authorization'] = `Bearer ${token}`;
  }
  const fullUrl = endpoint.startsWith('http') ? endpoint : `http://localhost:8000/api${endpoint}`;
  const result = await makeRequest(fullUrl, { ...options, headers });
  
  // Jika result null karena HTML overwrite, hentikan
  if (result === null) return null;
  
  // Handle unauthorized dari pesan JSON (middleware Laravel mengembalikan {message: "Unauthenticated."})
  // Catatan: Login sukses tidak memiliki field 'message' dengan nilai tersebut.
  if (result && result.message === 'Unauthenticated.') {
    localStorage.removeItem('token');
    redirectToLogin();
    return null;
  }
  
  // Untuk debugging, jika response mengandung exception HTML (error 500 dll) sudah ditangani oleh makeRequest
  return result;
}