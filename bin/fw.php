<?php
// Lista los firmwares de este directorio en JSON. Los nombres los pone
// scripts/build_fw.ps1: astrok_<motor>_v<x.y.z>_<yyyymmdd>.bin
// Los que no siguen ese patron se ignoran.

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-cache');

$dir = __DIR__;
$ruta = str_replace('\\', '/', dirname($_SERVER['SCRIPT_NAME'] ?? '/'));
$base = (isset($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off' ? 'https' : 'http')
    . '://' . ($_SERVER['HTTP_HOST'] ?? 'localhost')
    . ($ruta === '.' ? '' : rtrim($ruta, '/')) . '/';

// Descripcion por motor; uno nuevo sin entrada aqui sale con la generica.
$descripciones = [
    '17HS4023' => [
        'en' => 'Stock NEMA 17 motor (17HS4023, 1.8 degrees). Works as rotator or focuser, set by the jumper.',
        'es' => 'Motor NEMA 17 de serie (17HS4023, 1,8 grados). Vale como rotador o enfocador segun el jumper.',
    ],
    '35BY48' => [
        'en' => 'ZWO EAF 35BY48 motor (12 V, 120:1 gearbox), meant as a focuser. The mode is still set by the jumper.',
        'es' => 'Motor 35BY48 del ZWO EAF (12 V, reductora 120:1), pensado como enfocador. El modo lo sigue decidiendo el jumper.',
    ],
];

$firmwares = [];
foreach (glob($dir . '/*.bin') as $ruta) {
    $nombre = basename($ruta);
    if (!preg_match('/^astrok_(.+)_v(\d+\.\d+\.\d+)_(\d{4})(\d{2})(\d{2})\.bin$/', $nombre, $m)) {
        continue;
    }
    $firmwares[] = [
        'file'     => $nombre,
        'url'      => $base . rawurlencode($nombre),
        'motor'    => $m[1],
        'description' => $descripciones[$m[1]] ?? [
            'en' => "AstroK firmware for the $m[1] motor.",
            'es' => "Firmware AstroK para el motor $m[1].",
        ],
        'version'  => $m[2],
        'date'     => "$m[3]-$m[4]-$m[5]",
        'size'     => filesize($ruta),
        'sha256'   => hash_file('sha256', $ruta),
        'modified' => gmdate('c', filemtime($ruta)),
    ];
}

// Lo mas reciente primero: version, luego fecha, luego motor.
usort($firmwares, function ($a, $b) {
    return version_compare($b['version'], $a['version'])
        ?: strcmp($b['date'], $a['date'])
        ?: strcmp($a['motor'], $b['motor']);
});

// Instaladores de la app en la misma carpeta (los nombres los pone
// ascom-driver/build.ps1). La app 1.4.3 y posteriores los usan para avisar de
// que hay una version nueva; las anteriores ignoran la clave.
$apps = [];
foreach (glob($dir . '/*.exe') as $ruta) {
    $nombre = basename($ruta);
    if (!preg_match('/^AstroK(Focuser|Rotator)-Setup-(\d+\.\d+\.\d+)\.exe$/', $nombre, $m)) {
        continue;
    }
    $apps[] = [
        'file'     => $nombre,
        'url'      => $base . rawurlencode($nombre),
        'product'  => strtolower($m[1]),
        'version'  => $m[2],
        'size'     => filesize($ruta),
        'sha256'   => hash_file('sha256', $ruta),
        'modified' => gmdate('c', filemtime($ruta)),
    ];
}
usort($apps, function ($a, $b) {
    return version_compare($b['version'], $a['version']) ?: strcmp($a['product'], $b['product']);
});

// Paquetes de los drivers INDI para Linux (los nombres los pone
// scripts/build_indi.ps1): indi-astrok_<x.y.z>_<arquitectura>.deb. Un solo
// paquete lleva enfocador y rotador.
$indi = [];
foreach (glob($dir . '/*.deb') as $ruta) {
    $nombre = basename($ruta);
    if (!preg_match('/^indi-astrok_(\d+\.\d+\.\d+)_([a-z0-9]+)\.deb$/', $nombre, $m)) {
        continue;
    }
    $indi[] = [
        'file'     => $nombre,
        'url'      => $base . rawurlencode($nombre),
        'version'  => $m[1],
        'arch'     => $m[2],
        'size'     => filesize($ruta),
        'sha256'   => hash_file('sha256', $ruta),
        'modified' => gmdate('c', filemtime($ruta)),
    ];
}
usort($indi, function ($a, $b) {
    return version_compare($b['version'], $a['version']) ?: strcmp($a['arch'], $b['arch']);
});

echo json_encode(['firmwares' => $firmwares, 'apps' => $apps, 'indi' => $indi], JSON_PRETTY_PRINT | JSON_UNESCAPED_SLASHES);
