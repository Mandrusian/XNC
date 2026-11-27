import Darwin
import Foundation

/// Launches and supervises the bundled upstream SearXNG server on 127.0.0.1.
final class LocalSearXNGManager {
    private var process: Process?
    private(set) var baseURL: String = ""
    private(set) var isReady: Bool = false

    var hasRuntime: Bool {
        locateRuntime() != nil
    }

    func start(onReady: @escaping (String) -> Void) {
        guard process == nil else { return }
        guard let paths = locateRuntime() else { return }

        let port = pickFreePort(preferred: 18888)
        let dataDir = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first?
            .appendingPathComponent("XNC/searxng", isDirectory: true)
            ?? FileManager.default.temporaryDirectory.appendingPathComponent("xnc-searxng", isDirectory: true)

        let proc = Process()
        proc.executableURL = paths.python
        proc.arguments = [
            paths.runner.path,
            "--port", "\(port)",
            "--data-dir", dataDir.path,
            "--parent-pid", "\(ProcessInfo.processInfo.processIdentifier)"
        ]
        var env = ProcessInfo.processInfo.environment
        env["PYTHONNOUSERSITE"] = "1"
        env["PYTHONDONTWRITEBYTECODE"] = "1"
        env["PYTHONUTF8"] = "1"
        proc.environment = env

        let devNull = FileHandle(forWritingAtPath: "/dev/null")
        proc.standardOutput = devNull
        proc.standardError = devNull

        do {
            try proc.run()
            self.process = proc
            self.baseURL = "http://127.0.0.1:\(port)"
            pollReadiness(urlString: self.baseURL, onReady: onReady)
        } catch {
            self.process = nil
            self.baseURL = ""
        }
    }

    func stop() {
        if let proc = process, proc.isRunning {
            proc.terminate()
        }
        process = nil
        isReady = false
    }

    private func pollReadiness(urlString: String, onReady: @escaping (String) -> Void) {
        guard let probeURL = URL(string: urlString + "/") else { return }
        DispatchQueue.global(qos: .utility).async { [weak self] in
            for _ in 0..<80 {
                guard let self, let proc = self.process, proc.isRunning else { return }
                let sem = DispatchSemaphore(value: 0)
                var ok = false
                var req = URLRequest(url: probeURL)
                req.timeoutInterval = 0.8
                let task = URLSession.shared.dataTask(with: req) { _, resp, _ in
                    if let http = resp as? HTTPURLResponse, (200..<500).contains(http.statusCode) {
                        ok = true
                    }
                    sem.signal()
                }
                task.resume()
                _ = sem.wait(timeout: .now() + 1.0)
                if ok {
                    DispatchQueue.main.async {
                        self.isReady = true
                        onReady(urlString)
                    }
                    return
                }
                Thread.sleep(forTimeInterval: 0.25)
            }
        }
    }

    private func locateRuntime() -> (python: URL, runner: URL)? {
        var roots: [URL] = []
        if let res = Bundle.main.resourceURL {
            roots.append(res.appendingPathComponent("searxng-runtime", isDirectory: true))
        }
        if let execDir = Bundle.main.executableURL?.deletingLastPathComponent() {
            roots.append(execDir.appendingPathComponent("searxng-runtime", isDirectory: true))
            roots.append(execDir.appendingPathComponent("../searxng-runtime", isDirectory: true))
            roots.append(execDir.appendingPathComponent("../../../macos/searxng-runtime", isDirectory: true))
        }
        let cwd = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
        roots.append(cwd.appendingPathComponent("searxng-runtime", isDirectory: true))
        roots.append(cwd.appendingPathComponent("macos/searxng-runtime", isDirectory: true))

        for root in roots {
            let py = root.appendingPathComponent("python/bin/python3")
            let runner = root.appendingPathComponent("run_searxng.py")
            if FileManager.default.isExecutableFile(atPath: py.path),
               FileManager.default.fileExists(atPath: runner.path) {
                return (py, runner)
            }
        }
        return nil
    }

    private func pickFreePort(preferred: UInt16) -> UInt16 {
        if isPortFree(preferred) { return preferred }
        let sock = Darwin.socket(AF_INET, SOCK_STREAM, 0)
        guard sock >= 0 else { return preferred }
        defer { Darwin.close(sock) }

        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = 0
        addr.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))

        let bound = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(sock, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bound == 0 else { return preferred }

        var len = socklen_t(MemoryLayout<sockaddr_in>.size)
        let got = withUnsafeMutablePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.getsockname(sock, $0, &len)
            }
        }
        return got == 0 ? UInt16(bigEndian: addr.sin_port) : preferred
    }

    private func isPortFree(_ port: UInt16) -> Bool {
        let sock = Darwin.socket(AF_INET, SOCK_STREAM, 0)
        guard sock >= 0 else { return false }
        defer { Darwin.close(sock) }
        var yes: Int32 = 1
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &yes, socklen_t(MemoryLayout<Int32>.size))

        var addr = sockaddr_in()
        addr.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        addr.sin_family = sa_family_t(AF_INET)
        addr.sin_port = port.bigEndian
        addr.sin_addr = in_addr(s_addr: inet_addr("127.0.0.1"))

        let bound = withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.bind(sock, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        return bound == 0
    }
}
