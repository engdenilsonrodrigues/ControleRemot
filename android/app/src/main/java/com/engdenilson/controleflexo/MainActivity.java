package com.engdenilson.controleflexo;

import android.Manifest;
import android.annotation.SuppressLint;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothGatt;
import android.bluetooth.BluetoothGattCallback;
import android.bluetooth.BluetoothGattCharacteristic;
import android.bluetooth.BluetoothGattDescriptor;
import android.bluetooth.BluetoothGattService;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothProfile;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanFilter;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelUuid;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebSettings;
import android.webkit.WebView;

import java.util.ArrayList;
import java.util.List;
import java.util.UUID;

/**
 * Controle Flexo - replica o controle remoto Sleeve no celular e envia os
 * botoes ao ESP32 por Bluetooth Low Energy.
 *
 * Seguranca: os bits so ficam ativos enquanto o dedo esta na tela. Com botao
 * pressionado o app reenvia a cada 150 ms; o ESP32 zera tudo se ficar 500 ms
 * sem receber, se o Bluetooth cair, ou se o app for para segundo plano.
 */
@SuppressLint("MissingPermission")
public class MainActivity extends Activity {

    static final UUID SERVICO = UUID.fromString("6e7a0001-5f1e-4c7b-9a51-0f1e7c0a5e01");
    static final UUID CMD     = UUID.fromString("6e7a0002-5f1e-4c7b-9a51-0f1e7c0a5e01");
    static final UUID STATUS  = UUID.fromString("6e7a0003-5f1e-4c7b-9a51-0f1e7c0a5e01");
    static final UUID CCCD    = UUID.fromString("00002902-0000-1000-8000-00805f9b34fb");

    static final int REQ_PERM = 1, REQ_BT = 2;
    static final long REENVIO_MS = 150, SCAN_MS = 10000;

    private final Handler h = new Handler(Looper.getMainLooper());
    private WebView web;
    private BluetoothAdapter adapter;
    private BluetoothLeScanner scanner;
    private BluetoothGatt gatt;
    private BluetoothGattCharacteristic chCmd, chStatus;

    private volatile int bits = 0;      // escrito pela tela (thread do JS)
    private int enviado = -1;
    private int emEnvio = 0;
    private long ultimoEnvio = 0, inicioEnvio = 0;
    private boolean ocupado = false, pronto = false, procurando = false, ativo = false;
    private int tentativasLeitura = 0;
    private String ultimoTexto = "";

    // ------------------------------------------------------------ ciclo de vida

    @SuppressLint("SetJavaScriptEnabled")
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        web.setBackgroundColor(0xFF0F1A22);
        web.setVerticalScrollBarEnabled(false);
        web.addJavascriptInterface(new Ponte(), "Android");
        web.loadUrl("file:///android_asset/painel.html");
        setContentView(web);

        BluetoothManager bm = (BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
        adapter = bm != null ? bm.getAdapter() : null;
    }

    @Override
    protected void onResume() {
        super.onResume();
        ativo = true;
        h.removeCallbacks(laco);
        h.post(laco);
        iniciar();
    }

    @Override
    protected void onPause() {
        super.onPause();
        ativo = false;
        bits = 0;
        h.removeCallbacksAndMessages(null);
        pararBusca();
        desconectar();          // ESP32 zera os comandos ao perder a conexao
    }

    // ------------------------------------------------------------ ponte com a tela

    class Ponte {
        @JavascriptInterface
        public void setBits(int b) {
            if (b != 0 && bits == 0) vibrar();
            bits = b;
        }

        @JavascriptInterface
        public void reconectar() {
            h.post(() -> { desconectar(); iniciar(); });
        }
    }

    private void vibrar() {
        try {
            Vibrator v = (Vibrator) getSystemService(VIBRATOR_SERVICE);
            if (v != null) v.vibrate(VibrationEffect.createOneShot(15, VibrationEffect.DEFAULT_AMPLITUDE));
        } catch (Exception ignored) { }
    }

    private void telaDesconectada(String txt) {
        ultimoTexto = txt;
        js("appStatus({ble:false,txt:" + jsStr(txt) + "})");
    }

    private void js(String codigo) {
        h.post(() -> { if (web != null) web.evaluateJavascript(codigo, null); });
    }

    private static String jsStr(String s) {
        return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'";
    }

    // ------------------------------------------------------------ permissoes / Bluetooth ligado

    private String[] permissoes() {
        if (Build.VERSION.SDK_INT >= 31)
            return new String[]{Manifest.permission.BLUETOOTH_SCAN, Manifest.permission.BLUETOOTH_CONNECT};
        return new String[]{Manifest.permission.ACCESS_FINE_LOCATION};
    }

    private void iniciar() {
        if (!ativo) return;
        if (adapter == null) { telaDesconectada("celular sem Bluetooth"); return; }

        List<String> faltando = new ArrayList<>();
        for (String p : permissoes())
            if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) faltando.add(p);
        if (!faltando.isEmpty()) {
            telaDesconectada("permita o acesso ao Bluetooth");
            requestPermissions(faltando.toArray(new String[0]), REQ_PERM);
            return;
        }
        if (!adapter.isEnabled()) {
            telaDesconectada("ligue o Bluetooth");
            startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), REQ_BT);
            return;
        }
        if (gatt == null && !procurando) buscar();
    }

    @Override
    public void onRequestPermissionsResult(int req, String[] p, int[] r) {
        super.onRequestPermissionsResult(req, p, r);
        for (int x : r) if (x != PackageManager.PERMISSION_GRANTED) {
            telaDesconectada("sem permissao de Bluetooth - toque aqui");
            return;
        }
        iniciar();
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req == REQ_BT) {
            if (res == RESULT_OK) iniciar();
            else telaDesconectada("Bluetooth desligado - toque aqui");
        }
    }

    // ------------------------------------------------------------ busca do ESP32

    private final ScanCallback cbBusca = new ScanCallback() {
        @Override
        public void onScanResult(int tipo, ScanResult r) {
            if (!procurando) return;
            pararBusca();
            conectar(r.getDevice());
        }

        @Override
        public void onScanFailed(int erro) {
            procurando = false;
            telaDesconectada("falha na busca (" + erro + ") - toque aqui");
        }
    };

    private final Runnable fimBusca = () -> {
        if (procurando) {
            pararBusca();
            telaDesconectada("controle nao encontrado - tentando de novo");
            h.postDelayed(this::iniciar, 2000);
        }
    };

    private void buscar() {
        scanner = adapter.getBluetoothLeScanner();
        if (scanner == null) { telaDesconectada("ligue o Bluetooth"); return; }
        List<ScanFilter> filtros = new ArrayList<>();
        filtros.add(new ScanFilter.Builder().setServiceUuid(new ParcelUuid(SERVICO)).build());
        ScanSettings cfg = new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
        procurando = true;
        telaDesconectada("procurando controle...");
        scanner.startScan(filtros, cfg, cbBusca);
        h.postDelayed(fimBusca, SCAN_MS);
    }

    private void pararBusca() {
        h.removeCallbacks(fimBusca);
        if (procurando && scanner != null) {
            try { scanner.stopScan(cbBusca); } catch (Exception ignored) { }
        }
        procurando = false;
    }

    // ------------------------------------------------------------ conexao GATT

    private void conectar(BluetoothDevice d) {
        telaDesconectada("conectando...");
        gatt = d.connectGatt(this, false, cbGatt, BluetoothDevice.TRANSPORT_LE);
    }

    private void desconectar() {
        pronto = false;
        ocupado = false;
        enviado = -1;
        if (gatt != null) {
            try { gatt.disconnect(); gatt.close(); } catch (Exception ignored) { }
            gatt = null;
        }
        chCmd = chStatus = null;
    }

    private final BluetoothGattCallback cbGatt = new BluetoothGattCallback() {
        @Override
        public void onConnectionStateChange(BluetoothGatt g, int st, int novo) {
            h.post(() -> {
                if (g != gatt) { g.close(); return; }
                if (novo == BluetoothProfile.STATE_CONNECTED) {
                    g.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_HIGH);
                    g.discoverServices();
                } else {
                    desconectar();
                    telaDesconectada("controle desconectado - reconectando");
                    if (ativo) h.postDelayed(MainActivity.this::iniciar, 1500);
                }
            });
        }

        @Override
        public void onServicesDiscovered(BluetoothGatt g, int st) {
            h.post(() -> {
                BluetoothGattService s = g.getService(SERVICO);
                if (s == null) { telaDesconectada("dispositivo errado"); desconectar(); return; }
                chCmd = s.getCharacteristic(CMD);
                chStatus = s.getCharacteristic(STATUS);
                if (chCmd == null || chStatus == null) { telaDesconectada("firmware incompativel"); desconectar(); return; }
                g.setCharacteristicNotification(chStatus, true);
                BluetoothGattDescriptor d = chStatus.getDescriptor(CCCD);
                if (d != null) {
                    d.setValue(BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE);
                    g.writeDescriptor(d);
                } else {
                    lerStatus(g);
                }
            });
        }

        @Override
        public void onDescriptorWrite(BluetoothGatt g, BluetoothGattDescriptor d, int st) {
            // A leitura do status e protegida: dispara o pareamento (PIN) na primeira vez
            h.post(() -> { tentativasLeitura = 0; telaDesconectada("pareando (digite o PIN se pedir)..."); lerStatus(g); });
        }

        @Override
        public void onCharacteristicRead(BluetoothGatt g, BluetoothGattCharacteristic c, int st) {
            byte[] v = c.getValue();
            h.post(() -> {
                if (st == BluetoothGatt.GATT_SUCCESS) {
                    pronto = true;
                    if (v != null) mostrarStatus(v);
                } else if (++tentativasLeitura < 8) {
                    h.postDelayed(() -> { if (gatt == g) lerStatus(g); }, 2500);  // aguardando o PIN
                } else {
                    telaDesconectada("PIN recusado - toque para tentar de novo");
                    desconectar();
                }
            });
        }

        @Override
        public void onCharacteristicWrite(BluetoothGatt g, BluetoothGattCharacteristic c, int st) {
            h.post(() -> {
                ocupado = false;
                if (st == BluetoothGatt.GATT_SUCCESS) enviado = emEnvio;
            });
        }

        // Android 13+
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c, byte[] v) {
            if (STATUS.equals(c.getUuid())) h.post(() -> mostrarStatus(v));
        }

        // Android 12 e anteriores
        @Override
        public void onCharacteristicChanged(BluetoothGatt g, BluetoothGattCharacteristic c) {
            byte[] v = c.getValue();
            if (STATUS.equals(c.getUuid()) && v != null) h.post(() -> mostrarStatus(v));
        }
    };

    private void lerStatus(BluetoothGatt g) {
        if (g == gatt && chStatus != null) g.readCharacteristic(chStatus);
    }

    private void mostrarStatus(byte[] v) {
        if (v.length < 6) return;
        int vel = (short) ((v[4] & 0xFF) | ((v[5] & 0xFF) << 8));
        js("appStatus({ble:true,l:" + (v[0] & 0xFF) + ",w:" + (v[1] & 0xFF)
                + ",s:" + (v[2] & 0xFF) + ",t:" + (v[3] & 0xFF) + ",v:" + vel + "})");
    }

    // ------------------------------------------------------------ envio dos comandos

    private final Runnable laco = new Runnable() {
        @Override
        public void run() {
            long agora = System.currentTimeMillis();
            if (ocupado && agora - inicioEnvio > 1000) ocupado = false;   // escrita perdida
            if (pronto && !ocupado && gatt != null && chCmd != null) {
                int b = bits;
                if (b != enviado || (b != 0 && agora - ultimoEnvio >= REENVIO_MS)) {
                    chCmd.setWriteType(BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT);
                    chCmd.setValue(new byte[]{(byte) b, (byte) (b >> 8), (byte) (b >> 16), (byte) (b >> 24)});
                    if (gatt.writeCharacteristic(chCmd)) {
                        ocupado = true;
                        emEnvio = b;
                        ultimoEnvio = inicioEnvio = agora;
                    }
                }
            }
            if (ativo) h.postDelayed(this, 30);
        }
    };
}
