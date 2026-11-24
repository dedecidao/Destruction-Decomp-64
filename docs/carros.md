# Guia dos carros: status, marchas e o que cada número faz

Este guia mostra **todos os números que definem cada carro** do jogo, de forma simples. Ele foi feito para responder a três perguntas:
- Qual é a diferença entre os carros?
- O que acontece se eu mudar um número?
- Como funcionam o motor e as marchas?

Os valores são os **originais do jogo**, lidos direto da ROM. Nada aqui muda o jogo: o modo Play Fidelity continua igual ao original.

## Resumo em 30 segundos

- O jogo tem **24 carros**.
- **Os 12 primeiros (os liberados no início) são iguais por dentro.** Só muda a aparência. Por isso a tela de seleção mostra as mesmas barras para todos eles. Isso não é bug.
- Os carros **13 a 24** (os desbloqueáveis) têm números próprios.
- O carro troca de marcha sozinho, de verdade, da 1ª até a 5ª, e chega à velocidade máxima em 5ª (uns 216 km/h nos carros 1 a 12).
- O **som** do motor tem um câmbio separado, que não muda a força do carro.

## Glossário (em palavras simples)

| Termo | O que é |
|---|---|
| **Massa** | O peso do carro em kg. Mais pesado = arranca e freia pior, mas empurra mais os outros nas batidas. |
| **Torque** | A força do motor. "Na arrancada" é a força parado; "máximo" é o pico (a 3500 rpm). |
| **rpm** | Rotações do motor por minuto. Nos carros deste jogo vai de 750 até 6500. |
| **Relação de marcha** | Quanto a marcha multiplica a força. 1ª tem número alto (muita força, pouca velocidade); 5ª tem número baixo (pouca força, muita velocidade). |
| **Relação final** | Um multiplicador que vale para todas as marchas. |
| **Tração** | A aderência dos pneus. Mais tração = segura mais nas curvas e patina menos. |
| **Freio-motor** | O quanto o carro perde velocidade quando você solta o acelerador. |
| **Fora da pista** | A aderência na terra e na grama. |
| **Resposta da direção** | A rapidez com que o volante vira. |
| **Defesa / ataque** | Para cada lado do carro: defesa alta sofre menos dano; ataque alto causa mais dano no carro em que bate. |
| **Golpes** | Quantas batidas uma parte do carro aguenta até ficar destruída. |

## As tabelas

Na primeira linha de cada tabela, **"1 a 12"** vale para todos os carros iniciais, porque eles são iguais.

### 1. Motor e peso

O que faz o carro **acelerar**. A coluna "força por peso" é o torque máximo dividido pela massa (× 1000): quanto maior, mais o carro arranca.

| Carro | Massa (kg) | Torque na arrancada | Torque máximo | Força por peso |
|---|---|---|---|---|
| **1 a 12** (todos iguais) | 910 | 165 | 170 | 187 |
| **13** | 600 | 190 | 265 | 442 |
| **14** | 1130 | 300 | 500 | 442 |
| **15** | 1350 | 200 | 220 | 163 |
| **16** | 1000 | 170 | 180 | 180 |
| **17** | 1490 | 195 | 230 | 154 |
| **18** | 910 | 255 | 455 | 500 |
| **19** | 1060 | 185 | 225 | 212 |
| **20** | 1000 | 325 | 415 | 415 |
| **21** | 2000 | 215 | 330 | 165 |
| **22** | 1100 | 70 | 135 | 123 |
| **23** | 980 | 215 | 350 | 357 |
| **24** | 1000 | 305 | 335 | 335 |

### 2. Câmbio

As **relações** de cada marcha (quanto a marcha multiplica a força) e a **relação final** (multiplica todas). Números maiores dão mais arrancada e menos velocidade naquela marcha. "Vel. máx." é a velocidade em que o som chega à última marcha, que no teste coincidiu com a velocidade final do carro.

| Carro | Ré | 1ª | 2ª | 3ª | 4ª | 5ª | Final | Vel. máx. (mph) |
|---|---|---|---|---|---|---|---|---|
| **1 a 12** (todos iguais) | -3.8 | 12 | 10.5 | 9 | 7 | 2.5 | 1.9 | 135 |
| **13** | -3.8 | 10.5 | 8.7 | 7 | 3.5 | 2.6 | 2.1 | 110 |
| **14** | -3.8 | 11.8 | 9.3 | 8 | 6.6 | 2.9 | 1.7 | 135 |
| **15** | -7.8 | 12 | 10.5 | 8 | 5.5 | 3 | 1.8 | 135 |
| **16** | -3.8 | 11.5 | 9.6 | 8.1 | 6.1 | 2.5 | 2 | 110 |
| **17** | -3.8 | 11.8 | 9.9 | 8.2 | 6.3 | 2.8 | 1.9 | 135 |
| **18** | -6.1 | 16.4 | 10.4 | 7.7 | 5.5 | 2.7 | 1.8 | 115 |
| **19** | -3.8 | 10.1 | 9 | 7.7 | 6 | 2.6 | 1.6 | 135 |
| **20** | -3.8 | 17.4 | 12 | 10.4 | 8 | 3.1 | 1.45 | 155 |
| **21** | -7.8 | 15.5 | 12 | 9.9 | 7.7 | 3.5 | 1.8 | 150 |
| **22** | -9.3 | 13.5 | 11.5 | 9 | 7 | 2.5 | 1.9 | 110 |
| **23** | -3.8 | 12 | 10.5 | 9 | 7 | 2.5 | 1.9 | 135 |
| **24** | -3.8 | 11.1 | 9.6 | 8.1 | 6 | 2.5 | 1.8 | 135 |

### 3. Pilotagem

O que faz o carro **fazer curva e parar**. Valores maiores = mais forte. O volante "lento" é o ângulo das rodas parado e o "rápido" a 100 km/h.

| Carro | Tração | Freio | Freio-motor | Fora da pista | Resposta da direção | Volante lento | Volante rápido |
|---|---|---|---|---|---|---|---|
| **1 a 12** (todos iguais) | 1 | 2 | 1.6 | 0.9 | 2.1 | 65 | 8 ou 7 |
| **13** | 1.7 | 1.7 | 1.6 | 3 | 0.9 | 50 | 7 |
| **14** | 1 | 2 | 2 | 1 | 1.1 | 50 | 7 |
| **15** | 0.75 | 1.4 | 1.3 | 0.9 | 0.7 | 45 | 6 |
| **16** | 1 | 1.2 | 1.2 | 1.7 | 0.8 | 45 | 8 |
| **17** | 1 | 1.4 | 0.8 | 0.6 | 0.5 | 60 | 7 |
| **18** | 1 | 2 | 1.6 | 0.9 | 0.9 | 50 | 6 |
| **19** | 1 | 2 | 1 | 1.8 | 0.76 | 55 | 6.5 |
| **20** | 1 | 2 | 3 | 1 | 1.3 | 45 | 7 |
| **21** | 1.2 | 1 | 2 | 3 | 0.6 | 50 | 6 |
| **22** | 1 | 0.5 | 0.9 | 1.2 | 0.9 | 60 | 6 |
| **23** | 1 | 1.7 | 1.9 | 0.9 | 0.6 | 45 | 7 |
| **24** | 1 | 3 | 2 | 1 | 1.2 | 45 | 6 |

### 4. Suspensão e tamanho

Como o carro **balança e ocupa espaço** nas batidas. O tamanho é a metade do comprimento × altura × largura, em metros.

| Carro | Mola (rigidez) | Amortecedor | Altura | Tamanho |
|---|---|---|---|---|
| **1 a 12** (todos iguais) | 41000 | 0.65 | 0.44 | 2.61 × 0.91 × 1.11 |
| **13** | 17000 | 0.45 | 0.52 | 2.5 × 0.65 × 1.2 |
| **14** | 51000 | 0.65 | 0.44 | 2.5 × 0.65 × 1.05 |
| **15** | 40000 | 0.45 | 0.58 | 2.5 × 0.65 × 1.05 |
| **16** | 45000 | 0.65 | 0.44 | 2.5 × 0.65 × 1.05 |
| **17** | 45000 | 0.45 | 0.5 | 2.5 × 0.65 × 1.05 |
| **18** | 41000 | 0.65 | 0.44 | 2.5 × 0.65 × 1.05 |
| **19** | 50000 | 0.65 | 0.44 | 2.5 × 0.65 × 1.05 |
| **20** | 45000 | 0.65 | 0.44 | 2.61 × 0.91 × 1.11 |
| **21** | 90000 | 0.65 | 0.44 | 3 × 0.65 × 1.2 |
| **22** | 50000 | 0.65 | 0.44 | 2.5 × 0.65 × 1.05 |
| **23** | 44000 | 0.65 | 0.44 | 2.5 × 0.65 × 1.05 |
| **24** | 45000 | 0.65 | 0.44 | 2.5 × 0.65 × 1.05 |

### 5. Vida e dano

Cada lado do carro tem **defesa / ataque** (0 a 99): defesa alta sofre menos dano, ataque alto causa mais dano no outro. O lado direito e o esquerdo são iguais em todos os carros. "Golpes" é quantas batidas cada zona aguenta (frente / lado / traseira).

| Carro | Vida total | Frente | Dianteira lateral | Lateral | Traseira lateral | Traseira | Golpes |
|---|---|---|---|---|---|---|---|
| **1 a 12** (todos iguais) | 130 | 15 / 75 | 20 / 75 | 70 / 35 | 80 / 65 | 65 / 70 | 3 / 3 / 3 |
| **13** | 100 | 50 / 45 | 45 / 45 | 50 / 50 | 50 / 55 | 45 / 55 | 3 / 3 / 3 |
| **14** | 140 | 50 / 75 | 50 / 75 | 40 / 55 | 50 / 55 | 50 / 55 | 3 / 3 / 3 |
| **15** | 120 | 20 / 20 | 20 / 20 | 90 / 85 | 80 / 85 | 90 / 95 | 3 / 3 / 3 |
| **16** | 140 | 70 / 75 | 70 / 75 | 75 / 60 | 80 / 85 | 80 / 85 | 3 / 3 / 3 |
| **17** | 125 | 90 / 90 | 85 / 85 | 35 / 40 | 20 / 30 | 20 / 30 | 12 / 10 / 12 |
| **18** | 140 | 35 / 70 | 35 / 70 | 45 / 65 | 45 / 65 | 45 / 65 | 3 / 3 / 3 |
| **19** | 100 | 30 / 70 | 30 / 65 | 75 / 65 | 80 / 65 | 80 / 75 | 3 / 3 / 3 |
| **20** | 130 | 75 / 75 | 80 / 70 | 75 / 65 | 85 / 65 | 80 / 70 | 3 / 3 / 3 |
| **21** | 100 | 99 / 99 | 99 / 99 | 99 / 99 | 99 / 99 | 99 / 99 | 2 / 2 / 2 |
| **22** | 150 | 80 / 35 | 80 / 35 | 80 / 35 | 80 / 35 | 80 / 35 | 3 / 3 / 3 |
| **23** | 140 | 25 / 85 | 25 / 85 | 75 / 35 | 80 / 65 | 80 / 70 | 12 / 10 / 12 |
| **24** | 140 | 80 / 80 | 70 / 75 | 10 / 15 | 40 / 50 | 50 / 60 | 3 / 2 / 3 |

### 6. Barras da tela de seleção

O que o jogo mostra (0 a 19). Vem de uma tabela fixa no código, que não é calculada dos números acima.

| Carro | Velocidade | Aceleração | Controle | Durabilidade |
|---|---|---|---|---|
| **1 a 12** (todos iguais) | 14 | 9 | 19 | 11 |
| **13** | 8 | 18 | 15 | 9 |
| **14** | 12 | 17 | 17 | 8 |
| **15** | 9 | 8 | 9 | 14 |
| **16** | 11 | 9 | 12 | 15 |
| **17** | 10 | 7 | 5 | 13 |
| **18** | 13 | 19 | 13 | 7 |
| **19** | 19 | 11 | 11 | 10 |
| **20** | 17 | 16 | 18 | 16 |
| **21** | 4 | 5 | 7 | 19 |
| **22** | 13 | 3 | 14 | 17 |
| **23** | 14 | 15 | 10 | 6 |
| **24** | 16 | 14 | 16 | 12 |

## Como as marchas funcionam

O carro tem **dois câmbios** separados.

### O câmbio de verdade (o que move o carro)

1. O jogo mede a velocidade da roda e calcula o rpm do motor na marcha atual.
2. Se o rpm passa de **85% do máximo** (cerca de 5500), sobe uma marcha. Se cai abaixo de **25%** (cerca de 1600), desce uma.
3. A força que empurra o carro é **a força do motor naquele rpm × a relação da marcha × a relação final**. Por isso a força cai a cada troca.

Foi isso que o teste com o carro 1 e o acelerador no fundo mostrou:

| O que aconteceu | Velocidade |
|---|---|
| Passou para a 2ª | ~25 km/h (fica alternando entre 1ª e 2ª enquanto a roda patina) |
| Passou para a 3ª | ~45 km/h |
| Passou para a 4ª | ~52 km/h |
| Passou para a 5ª | ~67 km/h (as quatro trocas em pouco mais de 1 segundo) |
| Velocidade máxima, em 5ª | ~216 km/h (134 mph), com o motor quase no limite (6414 de 6500 rpm) |

Repare que a 4ª tem relação 7 e a 5ª tem 2,5. Por isso o carro chega rápido à 5ª e passa quase a corrida toda nela.

### O câmbio do som (o que você ouve)

O som do motor troca de marcha conforme a velocidade: em **30, 60, 90 e 115 mph**. Cada troca baixa o rpm do som em uns 800, sem tirar força do carro. É por isso que a troca quase não se ouve.

## Se eu mudar este número, o que acontece?

| Se eu aumentar... | O carro... |
|---|---|
| Torque (na arrancada ou máximo) | Acelera mais forte, em todas as marchas. |
| Massa | Acelera e freia pior, mas empurra mais os outros nas batidas e é menos empurrado. |
| Relações das marchas | Ganha força naquela marcha, mas chega antes ao limite dela (troca mais cedo). |
| Relação final | Arranca melhor e perde velocidade máxima. |
| rpm máximo (6500) | Estica as marchas e ganha velocidade máxima. |
| Tração | Segura mais nas curvas e patina menos na arrancada. |
| Freio | Para em menos espaço. Se for exagerado, as rodas travam. |
| Freio-motor | Perde velocidade mais rápido quando você solta o acelerador. |
| Resposta da direção | Vira mais rápido. Se for exagerado, fica nervoso. |
| Volante lento / rápido | Vira mais fechado em baixa / alta velocidade. |
| Mola / amortecedor | Balança e quica menos. |
| Vida total | Aguenta mais até ser destruído. |
| Defesa de um lado | Sofre menos dano naquele lado. |
| Ataque de um lado | Causa mais dano quando bate com aquele lado. |
| Golpes de uma zona | Aquela parte aguenta mais batidas (os faróis do Wrecked Metal também quebram por elas). |

**Os que mais pesam no desempenho:** torque e massa (aceleração), rpm máximo com a relação da 5ª e a final (velocidade máxima), tração e direção (controle), e vida, defesa e golpes (durabilidade).

## Para programadores: onde cada número fica

| O quê | Onde |
|---|---|
| Registro de cada carro | Recurso `D_80091AB4[carro]` (1000, 1100 ... 3300), com 0x2B0 bytes (`CarStats`, `include/car.h`) |
| Cópia no carro em corrida | `Car_Create` (`src/auto/1E308.c`) copia o registro para `Car+0xBEC` |
| Câmbio da física | `Car_SelectGear` (`src/auto/3A250.c`): marcha em `Car+0xA58`, rpm em `Car+0xA54` |
| Força do motor | `Car_GetDriveTorque` (`src/auto/3A3FC.c`) |
| Câmbio do som | `Car_UpdateEngineRpm` (`src/auto/3A558.c`): marcha em `Car+0xBA4`, rpm em `Car+0xBA8` |
| Barras da seleção | `CarSelect_SetStatBars` (`src/auto/42D14.c`) |
| Texto de pontos fortes | `D_8009694C` (`src/auto/42A44.c`) |
| Leitor no port | `port/game/wm_cars.c`: `WM_CarRecord(id)` |

| Offset no registro | Campo |
|---|---|
| 0x004 | modelo (0-23) |
| 0x008 | vida total |
| 0x00C / 0x010 / 0x014 / 0x018 / 0x01C | tração / freio / freio-motor / fora da pista / resposta da direção |
| 0x024 / 0x028 | rpm máximo / rpm de marcha lenta |
| 0x02C / 0x030 / 0x034 | torque na arrancada / torque máximo / rpm do torque máximo |
| 0x03C, 0x040-0x054 | nº de marchas (6), relações R e 1ª-5ª |
| 0x058 | relação final |
| 0x05C, 0x060-0x074 | nº de marchas do som (6), velocidades de troca do som em mph |
| 0x078 | massa (o jogo limita a 500-2000) |
| 0x084 | meia-caixa: comprimento, altura, largura |
| 0x090 / 0x094 | volante lento / rápido |
| 0x0B0 / 0x0B4 / 0x0B8 / 0x0BC / 0x0C0 | mola / amortecedor / altura / mola mínima / mola máxima |
| 0x0C8-0x124 | posições das rodas |
| 0x128 / 0x170 | lanternas traseiras / faróis dianteiros |
| 0x1E8-0x2AC | 10 zonas de dano × 0x14: id, defesa, ataque, golpes, 0 (zonas: frente, dianteira dir., lateral dir., traseira dir., dianteira esq., lateral esq., traseira esq., traseira, 8 e 9 ainda não identificadas) |

## Ferramentas de teste (PC, só ligam com a variável)

| Variável | O que faz |
|---|---|
| `DD64_DUMPCARS=arquivo` | Grava o registro inteiro dos 24 carros, número por número |
| `DD64_GEARLOG=1` | Registra no log a marcha, o rpm e a velocidade do carro do jogador, a cada troca |
