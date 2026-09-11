/**
 * PatternFlow Data Sheet — silent offline cache.
 * First visit downloads all app files. No permission prompt.
 * After a site update, bump CACHE_NAME (v1 → v2) so old caches are replaced.
 */
const CACHE_NAME = 'pf-datasheet-v6';

const LOCAL_FILES = [
  './',
  './index.html',
  './style.css',
  './script.js',
  './manifest.json',
  './logo.ico',
  './imh.png'
];

const CDN_FILES = [
  'https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/css/all.min.css',
  'https://cdnjs.cloudflare.com/ajax/libs/font-awesome/6.4.0/webfonts/fa-solid-900.woff2',
  'https://cdnjs.cloudflare.com/ajax/libs/html2pdf.js/0.10.1/html2pdf.bundle.min.js',
  'https://fonts.googleapis.com/css2?family=Poppins:wght@300;400;500;600;700&display=swap'
];

const ONLINE_ONLY_HOSTS = [
  'drive.google.com',
  'docs.google.com',
  'accounts.google.com'
];

function canCacheUrl(url) {
  try {
    const host = new URL(url, self.location.origin).hostname;
    return !ONLINE_ONLY_HOSTS.includes(host);
  } catch (e) {
    return false;
  }
}

async function addToCache(cache, url) {
  try {
    const request = new Request(url, { mode: 'cors', credentials: 'omit' });
    const response = await fetch(request);
    if (!response || !response.ok) return;
    await cache.put(request, response.clone());
    return response;
  } catch (e) {
    // Missing optional assets (logo/sponsor image) must not block install
  }
}

async function cacheLinkedAssets(cache, cssResponse, cssUrl) {
  if (!cssResponse) return;
  try {
    const text = await cssResponse.clone().text();
    const matches = [...text.matchAll(/url\((['"]?)([^'")]+)\1\)/g)];
    await Promise.all(matches.map((m) => {
      const raw = m[2].split(' ')[0].replace(/\\/g, '');
      const abs = new URL(raw, cssUrl).href;
      return addToCache(cache, abs);
    }));
  } catch (e) {
    // Font file discovery is best-effort
  }
}

self.addEventListener('install', (event) => {
  event.waitUntil((async () => {
    const cache = await caches.open(CACHE_NAME);
    await Promise.all(LOCAL_FILES.map((url) => addToCache(cache, url)));

    for (const url of CDN_FILES) {
      const response = await addToCache(cache, url);
      if (response && url.includes('.css')) {
        await cacheLinkedAssets(cache, response, url);
      }
    }

    self.skipWaiting();
  })());
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    const keys = await caches.keys();
    await Promise.all(keys.filter((key) => key !== CACHE_NAME).map((key) => caches.delete(key)));
    await self.clients.claim();
  })());
});

self.addEventListener('fetch', (event) => {
  if (event.request.method !== 'GET') return;
  if (!canCacheUrl(event.request.url)) return;

  event.respondWith((async () => {
    const cached = await caches.match(event.request, { ignoreSearch: true });
    if (cached) return cached;

    try {
      const response = await fetch(event.request);
      if (response && response.ok && response.type !== 'opaque') {
        const cache = await caches.open(CACHE_NAME);
        cache.put(event.request, response.clone());
      }
      return response;
    } catch (e) {
      if (event.request.mode === 'navigate') {
        return (await caches.match('./index.html')) || (await caches.match('./'));
      }
      return new Response('', { status: 503, statusText: 'Offline' });
    }
  })());
});
