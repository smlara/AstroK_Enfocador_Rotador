#include "astrok_canal.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <thread>
#include <unistd.h>

Canal::~Canal()
{
    Canal::cerrar();
}

void Canal::cerrar()
{
    if (fdLectura >= 0)
        ::close(fdLectura);
    fdLectura = -1;
    resto.clear();
}

bool Canal::leerHasta(char fin, int timeoutMs, std::string &salida)
{
    auto limite = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;)
    {
        size_t p = resto.find(fin);
        if (p != std::string::npos)
        {
            salida = resto.substr(0, p);
            resto.erase(0, p + 1);
            return true;
        }
        if (fdLectura < 0 || caido)
            return false;

        int quedan = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                         limite - std::chrono::steady_clock::now()).count();
        if (quedan <= 0)
            return false;

        pollfd pfd { fdLectura, POLLIN, 0 };
        int r = ::poll(&pfd, 1, quedan);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return false;
        // POLLHUP sin POLLIN: el USB se ha desenchufado o BlueZ ha cerrado
        // el socket de notificaciones al caerse la conexión.
        if (!(pfd.revents & POLLIN))
        {
            caido = true;
            return false;
        }

        // 512 cubre una notificación BLE entera (MTU máximo 517).
        char buf[512];
        ssize_t n = ::read(fdLectura, buf, sizeof(buf));
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        if (n <= 0)
        {
            caido = true;
            return false;
        }
        resto.append(buf, (size_t)n);
    }
}

void Canal::vaciar()
{
    resto.clear();
    if (fdLectura < 0)
        return;
    char buf[512];
    pollfd pfd { fdLectura, POLLIN, 0 };
    while (::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
    {
        if (::read(fdLectura, buf, sizeof(buf)) <= 0)
            break;
    }
}

// ---- USB ----------------------------------------------------------------

CanalSerie *CanalSerie::abrir(const std::string &puerto, std::string &error)
{
    int fd = ::open(puerto.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
    {
        error = puerto + ": " + std::strerror(errno);
        return nullptr;
    }

    // Un solo programa por equipo: en Windows lo garantiza el COM, aquí no.
    // Dos programas sobre el mismo puerto se roban las respuestas.
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        ::close(fd);
        error = puerto + " is in use by another program";
        return nullptr;
    }

    termios t {};
    if (::tcgetattr(fd, &t) != 0)
    {
        error = puerto + ": not a serial port";
        ::close(fd);
        return nullptr;
    }
    ::cfmakeraw(&t);
    ::cfsetispeed(&t, B115200);
    ::cfsetospeed(&t, B115200);
    t.c_cflag |= CLOCAL | CREAD;
    ::tcsetattr(fd, TCSANOW, &t);

    // ---- LO IMPORTANTE ----
    // El USB Serial/JTAG del C3 solo entrega datos con DTR asertado. Se
    // levantan DTR y RTS a la vez y no se tocan más: bajar DTR con RTS alto
    // es la señal de reset del chip, y es justo lo que hace al conectar el
    // conector serie estándar de INDI (por eso este driver abre el puerto
    // por su cuenta).
    int lineas = TIOCM_DTR | TIOCM_RTS;
    ::ioctl(fd, TIOCMBIS, &lineas);

    // Si abrir el puerto ha reiniciado el equipo, el firmware atiende el
    // protocolo durante el arranque, pero se da margen y se descarta el banner.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ::tcflush(fd, TCIFLUSH);

    auto *c = new CanalSerie();
    c->fdLectura = fd;
    return c;
}

bool CanalSerie::escribir(const std::string &texto)
{
    if (fdLectura < 0 || caido)
        return false;

    size_t hecho = 0;
    while (hecho < texto.size())
    {
        ssize_t n = ::write(fdLectura, texto.data() + hecho, texto.size() - hecho);
        if (n > 0)
        {
            hecho += (size_t)n;
            continue;
        }
        if (n < 0 && errno != EAGAIN && errno != EINTR)
        {
            caido = true;
            return false;
        }
        pollfd pfd { fdLectura, POLLOUT, 0 };
        if (::poll(&pfd, 1, 3000) <= 0)
            return false;
    }
    return true;
}
