# Welcome Animation — removida

A funcionalidade Welcome foi removida do firmware, da API e do PWA na versão
`0.7.6`, por decisão do usuário. A inicialização BLE dos dois SP624E fazia a
animação começar vários segundos depois de os faróis receberem alimentação,
produzindo uma experiência visual inadequada.

Não existem mais execução automática, configuração NVS, endpoints REST,
eventos WebSocket, rota `/animations` ou ação Welcome no botão D. Police
permanece como animação temporária, com Strict Sync e restauração verificada.
