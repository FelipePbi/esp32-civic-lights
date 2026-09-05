# Uso no iPhone

## Conectar e abrir

1. Ligue o ESP32 e os dois controladores SP624E.
2. No iPhone, abra **Ajustes → Wi-Fi**.
3. Selecione **Civic-Lights**.
4. Use a senha `zaq12wsx`.
5. O aviso “Sem conexão com a internet” é esperado; permaneça nessa rede.
6. Abra o Safari em `http://192.168.4.1`.

O indicador “AO VIVO” representa o WebSocket iPhone ↔ ESP32. LEFT/RIGHT e o
banner do grupo representam a conexão BLE separadamente.

## Controles

- Arraste o plano de cor e o espectro; pedidos durante o gesto são limitados a
  um a cada 100 ms e o valor final é enviado imediatamente.
- O preview muda na hora. “Aplicando e verificando” permanece até o firmware
  confirmar ambos os lados.
- Se um lado estiver reconectando, o novo ajuste fica pendente e o lado saudável
  permanece inalterado.
- **Branco real** só fica ativo após `test-white` e confirmação visual.
- **Favorita** aplica o preset salvo. “Salvar cor atual como favorita” é a única
  ação que grava esse preset em NVS.

## Tela de Início

No Safari: **Compartilhar → Adicionar à Tela de Início**. O manifest, tema e
ícone são locais. Não há Service Worker nesta versão; o ESP32 precisa estar
ligado e o iPhone conectado ao `Civic-Lights`. Se o iOS abrir a página no Safari
em vez do modo standalone por ela usar HTTP local, isso não afeta REST nem
WebSocket.

## Diagnóstico rápido

A seção recolhível mostra firmware, uptime, RSSI, reconexões e clientes Wi-Fi.
“Ressincronizar” pede nova verificação sem derrubar deliberadamente o BLE.
