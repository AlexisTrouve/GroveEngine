#pragma once

#include "AssetManager.h"                 // IAsyncDecoder + DecodedImage (the contract this implements)
#include "../Resources/TextureLoader.h"   // decodeRgba — pure CPU/stb, no GPU call (safe off the render thread)

#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <vector>
#include <atomic>
#include <string>

namespace grove::assets {

/**
 * @brief Real IAsyncDecoder — decode PNGs on background worker thread(s), hand finished pixels back to the
 *   render thread via poll() (phase 3 async load).
 *
 * QUOI : des worker threads dépilent des jobs {id,path}, décodent en RGBA8 (TextureLoader::decodeRgba), et
 *   poussent le résultat dans une file "terminés" ; poll() (thread render) draine cette file -> l'AssetManager
 *   fait l'upload GPU. POURQUOI : le decode (lecture fichier + stb) est la partie LENTE du first-touch — le
 *   sortir du thread render supprime le hitch ; l'upload reste côté render car bgfx est single-thread.
 * COMMENT : file de jobs protégée par mutex + condvar ; le worker bloque sur la condvar tant qu'il n'y a rien.
 *   decodeRgba est pur CPU (aucun appel bgfx) -> parfaitement safe hors thread render. Le destructeur pose un
 *   flag stop, notifie tous les workers, et les joine (pas de thread orphelin au hot-reload du module).
 *   Deux mutex distincts (jobs vs terminés) -> un worker peut décoder pendant que le thread render poll().
 */
class ThreadedDecoder : public IAsyncDecoder {
public:
    explicit ThreadedDecoder(int workers = 1) {
        if (workers < 1) workers = 1;                 // toujours au moins un worker
        for (int i = 0; i < workers; ++i)
            m_threads.emplace_back([this] { workerLoop(); });
    }

    ~ThreadedDecoder() override {
        { std::lock_guard<std::mutex> lk(m_jobMx); m_stop = true; }   // signale l'arrêt sous le lock
        m_cv.notify_all();                                            // réveille tous les workers en attente
        for (auto& t : m_threads) if (t.joinable()) t.join();         // attend leur sortie propre
    }

    // Render thread: enqueue an off-thread decode and wake a worker.
    void request(const std::string& id, const std::string& path) override {
        { std::lock_guard<std::mutex> lk(m_jobMx); m_jobs.push_back({id, path}); }
        ++m_pending;
        m_cv.notify_one();
    }

    // Render thread: take everything decoded since the last poll (moves it out, leaves the queue empty).
    void poll(std::vector<DecodedImage>& out) override {
        std::lock_guard<std::mutex> lk(m_doneMx);
        for (auto& d : m_done) out.push_back(std::move(d));
        m_done.clear();
    }

    size_t pending() const override { return m_pending.load(); }

private:
    struct Job { std::string id, path; };

    void workerLoop() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lk(m_jobMx);
                m_cv.wait(lk, [this] { return m_stop || !m_jobs.empty(); });
                if (m_stop && m_jobs.empty()) return;    // arrêt demandé et plus rien à faire -> sortir
                job = std::move(m_jobs.front());
                m_jobs.pop_front();
            }
            // Decode OUTSIDE the lock — the slow part; other workers stay free to pick up jobs.
            DecodedImage d;
            d.id = job.id;
            d.ok = TextureLoader::decodeRgba(job.path, d.pixels, d.w, d.h);
            { std::lock_guard<std::mutex> lk(m_doneMx); m_done.push_back(std::move(d)); }
            --m_pending;
        }
    }

    std::vector<std::thread> m_threads;
    std::mutex m_jobMx;                       // garde m_jobs + m_stop
    std::condition_variable m_cv;
    std::deque<Job> m_jobs;
    bool m_stop = false;
    std::mutex m_doneMx;                      // garde m_done
    std::vector<DecodedImage> m_done;
    std::atomic<size_t> m_pending{0};         // jobs encore en vol (diagnostic, lisible sans lock)
};

} // namespace grove::assets
